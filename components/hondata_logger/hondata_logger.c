#include "hondata_logger.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define LOGGER_MAX_SESSIONS 32U
#define LOGGER_SYNC_PERIOD_MS 1000U
#define LOGGER_FILE_VERSION 1U
#define LOGGER_RECORD_VERSION 1U

static const char *TAG = "hondata_logger";
static SemaphoreHandle_t logger_mutex;
static char logger_base_path[64];
static FILE *active_file;
static hondata_logger_status_t logger_status;
static hondata_state_t latest_state;
static uint32_t latest_uptime_ms;
static uint32_t last_sample_ms;
static uint32_t last_sync_ms;
static bool latest_valid;

typedef struct __attribute__((packed)) {
    uint8_t magic[4];
    uint8_t version;
    uint8_t log_name;
    uint16_t header_size;
    uint16_t record_size;
    uint16_t session_index;
    uint32_t start_uptime_ms;
    uint32_t reserved;
    uint32_t crc32;
} log_file_header_t;

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t gear;
    uint16_t record_size;
    uint32_t uptime_ms;
    uint32_t received_frame_mask;
    uint32_t last_frame_time_ms[HONDATA_CAN_FRAME_COUNT];
    uint16_t rpm;
    uint16_t speed_tenths_kph;
    uint16_t battery_mv;
    int16_t intake_temp_tenths_c;
    int16_t coolant_temp_tenths_c;
    uint16_t throttle_tenths_percent;
    int16_t map_gauge_tenths_kpa;
    uint16_t injector_duration_raw;
    int16_t ignition_angle_deg;
    uint16_t afr_thousandths;
    uint16_t knock_count;
    int16_t cam_angle_target_deg;
    int16_t cam_angle_actual_deg;
    uint16_t analog_mv[8];
    uint16_t ethanol_frequency_hz;
    uint16_t ethanol_duty_hundredths_percent;
    uint16_t ethanol_content_tenths_percent;
    uint32_t crc32;
} log_record_t;

_Static_assert(sizeof(log_file_header_t) == 24U,
               "Unexpected Hondata log header layout");
_Static_assert(sizeof(log_record_t) == 104U,
               "Unexpected Hondata log record layout");

static uint32_t crc32_bytes(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

static void session_path(char *path, size_t capacity, uint16_t session_index)
{
    snprintf(path, capacity, "%s/log%04u.bin", logger_base_path,
             (unsigned)session_index);
}

static uint16_t next_session_index(void)
{
    uint16_t highest = 0U;
    DIR *directory = opendir(logger_base_path);
    if (!directory) return 1U;

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        unsigned index = 0U;
        if (sscanf(entry->d_name, "log%4u.bin", &index) == 1 &&
            index > highest && index <= 9999U) {
            highest = (uint16_t)index;
        }
    }
    closedir(directory);
    return highest >= 9999U ? 1U : (uint16_t)(highest + 1U);
}

static void remove_expired_session(uint16_t new_index)
{
    if (new_index <= LOGGER_MAX_SESSIONS) return;
    char path[96];
    session_path(path, sizeof(path),
                 (uint16_t)(new_index - LOGGER_MAX_SESSIONS));
    if (remove(path) != 0) {
        ESP_LOGW(TAG, "Unable to remove expired session %s", path);
    }
}

esp_err_t hondata_logger_init(const char *sd_mount_path)
{
    if (logger_mutex) return ESP_OK;
    ESP_RETURN_ON_FALSE(sd_mount_path && sd_mount_path[0] != '\0',
                        ESP_ERR_INVALID_ARG, TAG, "SD mount path is required");
    logger_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(logger_mutex, ESP_ERR_NO_MEM, TAG,
                        "Logger mutex allocation failed");

    int length = snprintf(logger_base_path, sizeof(logger_base_path),
                          "%s/MDLOGS", sd_mount_path);
    ESP_RETURN_ON_FALSE(length > 0 && length < sizeof(logger_base_path),
                        ESP_ERR_INVALID_SIZE, TAG, "SD log path is too long");
    if (mkdir(logger_base_path, 0775) != 0) {
        DIR *existing = opendir(logger_base_path);
        if (!existing) {
            ESP_LOGE(TAG, "Unable to create SD log directory %s: errno %d",
                     logger_base_path, errno);
            return ESP_FAIL;
        }
        closedir(existing);
    }
    logger_status.mounted = true;
    logger_status.session_index = next_session_index();
    ESP_LOGI(TAG, "SD logging ready at %s; next session %u",
             logger_base_path, (unsigned)logger_status.session_index);
    return ESP_OK;
}

esp_err_t hondata_logger_set_recording(bool recording, uint8_t log_name,
                                       uint32_t uptime_ms)
{
    ESP_RETURN_ON_FALSE(logger_mutex && logger_status.mounted,
                        ESP_ERR_INVALID_STATE, TAG,
                        "Logger is not initialized");
    xSemaphoreTake(logger_mutex, portMAX_DELAY);
    if (recording == logger_status.recording) {
        xSemaphoreGive(logger_mutex);
        return ESP_OK;
    }

    if (!recording) {
        logger_status.recording = false;
        if (active_file) {
            fflush(active_file);
            fclose(active_file);
            active_file = NULL;
        }
        logger_status.session_index = next_session_index();
        xSemaphoreGive(logger_mutex);
        ESP_LOGI(TAG, "Recording stopped after %" PRIu32 " records",
                 logger_status.record_count);
        return ESP_OK;
    }

    const uint16_t session_index = logger_status.session_index;
    remove_expired_session(session_index);
    char path[96];
    session_path(path, sizeof(path), session_index);
    active_file = fopen(path, "wb");
    if (!active_file) {
        xSemaphoreGive(logger_mutex);
        ESP_LOGE(TAG, "Unable to create %s", path);
        return ESP_FAIL;
    }

    log_file_header_t header = {
        .magic = {'M', 'D', 'H', 'L'},
        .version = LOGGER_FILE_VERSION,
        .log_name = log_name,
        .header_size = sizeof(log_file_header_t),
        .record_size = sizeof(log_record_t),
        .session_index = session_index,
        .start_uptime_ms = uptime_ms,
    };
    header.crc32 = crc32_bytes(&header, offsetof(log_file_header_t, crc32));
    if (fwrite(&header, sizeof(header), 1U, active_file) != 1U ||
        fflush(active_file) != 0) {
        fclose(active_file);
        active_file = NULL;
        xSemaphoreGive(logger_mutex);
        ESP_LOGE(TAG, "Unable to write log header");
        return ESP_FAIL;
    }

    logger_status.recording = true;
    logger_status.record_count = 0U;
    logger_status.dropped_count = 0U;
    logger_status.session_index = session_index;
    last_sample_ms = uptime_ms - HONDATA_LOGGER_SAMPLE_PERIOD_MS;
    last_sync_ms = uptime_ms;
    xSemaphoreGive(logger_mutex);
    ESP_LOGI(TAG, "Recording session %u to %s", (unsigned)session_index,
             path);
    return ESP_OK;
}

void hondata_logger_sample(const hondata_state_t *state, uint32_t uptime_ms)
{
    if (!state || !logger_mutex ||
        (uint32_t)(uptime_ms - last_sample_ms) <
            HONDATA_LOGGER_SAMPLE_PERIOD_MS) {
        return;
    }

    xSemaphoreTake(logger_mutex, portMAX_DELAY);
    if (!logger_status.recording || !active_file ||
        state->received_frame_mask == 0U) {
        xSemaphoreGive(logger_mutex);
        return;
    }
    last_sample_ms = uptime_ms;

    log_record_t record = {
        .version = LOGGER_RECORD_VERSION,
        .gear = state->gear,
        .record_size = sizeof(log_record_t),
        .uptime_ms = uptime_ms,
        .received_frame_mask = state->received_frame_mask,
        .rpm = state->rpm,
        .speed_tenths_kph = state->speed_tenths_kph,
        .battery_mv = state->battery_mv,
        .intake_temp_tenths_c = state->intake_temp_tenths_c,
        .coolant_temp_tenths_c = state->coolant_temp_tenths_c,
        .throttle_tenths_percent = state->throttle_tenths_percent,
        .map_gauge_tenths_kpa = state->map_gauge_tenths_kpa,
        .injector_duration_raw = state->injector_duration_raw,
        .ignition_angle_deg = state->ignition_angle_deg,
        .afr_thousandths = state->afr_thousandths,
        .knock_count = state->knock_count,
        .cam_angle_target_deg = state->cam_angle_target_deg,
        .cam_angle_actual_deg = state->cam_angle_actual_deg,
        .ethanol_frequency_hz = state->ethanol_frequency_hz,
        .ethanol_duty_hundredths_percent =
            state->ethanol_duty_hundredths_percent,
        .ethanol_content_tenths_percent =
            state->ethanol_content_tenths_percent,
    };
    memcpy(record.last_frame_time_ms, state->last_frame_time_ms,
           sizeof(record.last_frame_time_ms));
    memcpy(record.analog_mv, state->analog_mv, sizeof(record.analog_mv));
    record.crc32 = crc32_bytes(&record, offsetof(log_record_t, crc32));

    if (fwrite(&record, sizeof(record), 1U, active_file) != 1U) {
        ++logger_status.dropped_count;
        ESP_LOGE(TAG, "Log write failed");
    } else {
        ++logger_status.record_count;
        latest_state = *state;
        latest_uptime_ms = uptime_ms;
        latest_valid = true;
    }
    if ((uint32_t)(uptime_ms - last_sync_ms) >= LOGGER_SYNC_PERIOD_MS) {
        last_sync_ms = uptime_ms;
        if (fflush(active_file) != 0) {
            ++logger_status.dropped_count;
            ESP_LOGE(TAG, "Log sync failed");
        }
    }
    xSemaphoreGive(logger_mutex);
}

void hondata_logger_get_status(hondata_logger_status_t *status)
{
    if (!status) return;
    if (!logger_mutex) {
        memset(status, 0, sizeof(*status));
        return;
    }
    xSemaphoreTake(logger_mutex, portMAX_DELAY);
    *status = logger_status;
    xSemaphoreGive(logger_mutex);
}

bool hondata_logger_get_latest(hondata_state_t *state, uint32_t *uptime_ms)
{
    if (!state || !logger_mutex) return false;
    xSemaphoreTake(logger_mutex, portMAX_DELAY);
    const bool valid = latest_valid;
    if (valid) {
        *state = latest_state;
        if (uptime_ms) *uptime_ms = latest_uptime_ms;
    }
    xSemaphoreGive(logger_mutex);
    return valid;
}