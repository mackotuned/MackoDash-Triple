#include "odometer.h"

#include <inttypes.h>

#include "esp_check.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define ODO_NAMESPACE "odometer"
#define ODO_KEY_A "odo_a"
#define ODO_KEY_B "odo_b"
#define TRIP_A_KEY "trip_a_m"
#define TRIP_B_KEY "trip_b_m"
#define ODO_SAVE_INTERVAL_METERS 100U
#define ODO_DEFAULT_METERS 3701491ULL
#define ODO_DISTANCE_DIVISOR 36000ULL

typedef struct {
    uint64_t meters;
    uint32_t version;
    uint32_t crc;
} odometer_record_t;

static const char *TAG = "odometer";
static nvs_handle_t odo_handle;
static uint64_t total_meters = ODO_DEFAULT_METERS;
static uint64_t trip_a_meters;
static uint64_t trip_b_meters;
static uint64_t last_saved_meters;
static uint64_t trip_a_last_saved;
static uint64_t trip_b_last_saved;
static uint64_t distance_remainder;
static uint32_t last_sample_time_ms;
static uint32_t current_version;
static bool use_slot_a = true;
static bool initialized;

static uint32_t record_crc(const odometer_record_t *record)
{
    return esp_crc32_le(0, (const uint8_t *)record,
                        sizeof(*record) - sizeof(record->crc));
}

static bool record_valid(const odometer_record_t *record)
{
    return record->version != 0U && record_crc(record) == record->crc;
}

static esp_err_t save_total(void)
{
    odometer_record_t record = {
        .meters = total_meters,
        .version = ++current_version,
    };
    record.crc = record_crc(&record);
    const char *key = use_slot_a ? ODO_KEY_A : ODO_KEY_B;

    esp_err_t error = nvs_set_blob(odo_handle, key, &record, sizeof(record));
    if (error == ESP_OK) {
        error = nvs_commit(odo_handle);
    }
    if (error == ESP_OK) {
        use_slot_a = !use_slot_a;
        last_saved_meters = total_meters;
        ESP_LOGI(TAG, "Saved %" PRIu64 " meters (v%" PRIu32 ")",
                 total_meters, current_version);
    }
    return error;
}

static esp_err_t save_trip(const char *key, uint64_t meters,
                           uint64_t *last_saved)
{
    esp_err_t error = nvs_set_i64(odo_handle, key, (int64_t)meters);
    if (error == ESP_OK) {
        error = nvs_commit(odo_handle);
    }
    if (error == ESP_OK) {
        *last_saved = meters;
    }
    return error;
}

esp_err_t odometer_init(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES ||
        error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase failed");
        error = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(error, TAG, "NVS init failed");
    ESP_RETURN_ON_ERROR(nvs_open(ODO_NAMESPACE, NVS_READWRITE, &odo_handle),
                        TAG, "Odometer namespace open failed");

    odometer_record_t record_a = {0};
    odometer_record_t record_b = {0};
    size_t record_size = sizeof(record_a);
    const bool valid_a =
        nvs_get_blob(odo_handle, ODO_KEY_A, &record_a, &record_size) == ESP_OK &&
        record_size == sizeof(record_a) && record_valid(&record_a);
    record_size = sizeof(record_b);
    const bool valid_b =
        nvs_get_blob(odo_handle, ODO_KEY_B, &record_b, &record_size) == ESP_OK &&
        record_size == sizeof(record_b) && record_valid(&record_b);

    if (valid_a && (!valid_b || record_a.version >= record_b.version)) {
        total_meters = record_a.meters;
        current_version = record_a.version;
        use_slot_a = false;
    } else if (valid_b) {
        total_meters = record_b.meters;
        current_version = record_b.version;
        use_slot_a = true;
    } else {
        current_version = 1U;
        ESP_LOGW(TAG, "No valid record; using default %" PRIu64 " meters",
                 total_meters);
    }
    if (total_meters < ODO_DEFAULT_METERS) {
        ESP_LOGW(TAG, "Stored value %" PRIu64
                      " is below baseline; restoring %" PRIu64 " meters",
                 total_meters, ODO_DEFAULT_METERS);
        total_meters = ODO_DEFAULT_METERS;
        ESP_RETURN_ON_ERROR(save_total(), TAG, "Baseline repair failed");
    }
    last_saved_meters = total_meters;

    int64_t trip_value = 0;
    if (nvs_get_i64(odo_handle, TRIP_A_KEY, &trip_value) == ESP_OK &&
        trip_value >= 0) {
        trip_a_meters = (uint64_t)trip_value;
    }
    if (nvs_get_i64(odo_handle, TRIP_B_KEY, &trip_value) == ESP_OK &&
        trip_value >= 0) {
        trip_b_meters = (uint64_t)trip_value;
    }
    trip_a_last_saved = trip_a_meters;
    trip_b_last_saved = trip_b_meters;
    initialized = true;

    ESP_LOGI(TAG, "Loaded %" PRIu64 " meters", total_meters);
    return ESP_OK;
}

void odometer_update(uint16_t speed_tenths_kph, bool speed_valid,
                     uint32_t sample_time_ms)
{
    if (!initialized || !speed_valid) {
        last_sample_time_ms = sample_time_ms;
        return;
    }
    if (last_sample_time_ms == 0U) {
        last_sample_time_ms = sample_time_ms;
        return;
    }

    const uint32_t elapsed_ms = sample_time_ms - last_sample_time_ms;
    last_sample_time_ms = sample_time_ms;
    if (elapsed_ms > 1000U) {
        return;
    }

    distance_remainder += (uint64_t)speed_tenths_kph * elapsed_ms;
    const uint64_t whole_meters = distance_remainder / ODO_DISTANCE_DIVISOR;
    distance_remainder %= ODO_DISTANCE_DIVISOR;
    total_meters += whole_meters;
    trip_a_meters += whole_meters;
    trip_b_meters += whole_meters;
}

esp_err_t odometer_periodic_save(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = ESP_OK;
    if (total_meters - last_saved_meters >= ODO_SAVE_INTERVAL_METERS) {
        error = save_total();
    }
    if (error == ESP_OK &&
        trip_a_meters - trip_a_last_saved >= ODO_SAVE_INTERVAL_METERS) {
        error = save_trip(TRIP_A_KEY, trip_a_meters, &trip_a_last_saved);
    }
    if (error == ESP_OK &&
        trip_b_meters - trip_b_last_saved >= ODO_SAVE_INTERVAL_METERS) {
        error = save_trip(TRIP_B_KEY, trip_b_meters, &trip_b_last_saved);
    }
    return error;
}

esp_err_t odometer_force_save(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(save_total(), TAG, "Total save failed");
    ESP_RETURN_ON_ERROR(save_trip(TRIP_A_KEY, trip_a_meters,
                                  &trip_a_last_saved),
                        TAG, "Trip A save failed");
    return save_trip(TRIP_B_KEY, trip_b_meters, &trip_b_last_saved);
}

uint64_t odometer_get_meters(void)
{
    return total_meters;
}

uint64_t odometer_get_trip_a_meters(void)
{
    return trip_a_meters;
}

uint64_t odometer_get_trip_b_meters(void)
{
    return trip_b_meters;
}

esp_err_t odometer_set_meters(uint64_t meters)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    total_meters = meters;
    distance_remainder = 0U;
    return save_total();
}

esp_err_t odometer_reset_trip_a(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    trip_a_meters = 0U;
    return save_trip(TRIP_A_KEY, trip_a_meters, &trip_a_last_saved);
}

esp_err_t odometer_reset_trip_b(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    trip_b_meters = 0U;
    return save_trip(TRIP_B_KEY, trip_b_meters, &trip_b_last_saved);
}