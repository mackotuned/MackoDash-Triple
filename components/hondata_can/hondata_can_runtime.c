#include "hondata_can.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include "cluster_board_config.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define HONDATA_CAN_LIVE_TIMEOUT_MS 1500U
#define HONDATA_CAN_RESCAN_TIMEOUT_MS 2000U
#define HONDATA_CAN_PROBE_TIME_MS 500U
#define HONDATA_CAN_TASK_STACK_SIZE 4096U
#define HONDATA_CAN_TASK_PRIORITY 8U

static const char *TAG = "hondata_can";
static const uint32_t can_bitrates[] = {1000000U, 500000U, 250000U, 125000U};
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static hondata_state_t decoded_state;
static hondata_can_diagnostics_t diagnostics;
static TaskHandle_t can_task_handle;
static uint32_t last_valid_frame_ms;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static twai_timing_config_t timing_for_bitrate(uint32_t bitrate)
{
    switch (bitrate) {
        case 1000000U:
            return (twai_timing_config_t)TWAI_TIMING_CONFIG_1MBITS();
        case 250000U:
            return (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS();
        case 125000U:
            return (twai_timing_config_t)TWAI_TIMING_CONFIG_125KBITS();
        case 500000U:
        default:
            return (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();
    }
}

static void set_controller_state(hondata_can_controller_state_t state,
                                 uint32_t bitrate)
{
    portENTER_CRITICAL(&state_lock);
    diagnostics.controller_state = state;
    diagnostics.bitrate = bitrate;
    portEXIT_CRITICAL(&state_lock);
}

static bool process_message(const twai_message_t *message)
{
    if (message->extd || message->rtr ||
        message->identifier < HONDATA_CAN_ID_FIRST ||
        message->identifier > HONDATA_CAN_ID_LAST) {
        return false;
    }

    const uint32_t sample_time_ms = now_ms();
    portENTER_CRITICAL(&state_lock);
    const hondata_decode_result_t result = hondata_decode_frame(
        &decoded_state, message->identifier, message->data,
        message->data_length_code, sample_time_ms);
    if (result == HONDATA_DECODE_OK) {
        last_valid_frame_ms = sample_time_ms;
        diagnostics.received_frame_count++;
    } else if (result == HONDATA_DECODE_INVALID_LENGTH) {
        diagnostics.invalid_length_count++;
    }
    portEXIT_CRITICAL(&state_lock);
    return result == HONDATA_DECODE_OK;
}

static void refresh_driver_diagnostics(void)
{
    twai_status_info_t status;
    if (twai_get_status_info(&status) != ESP_OK) {
        return;
    }

    portENTER_CRITICAL(&state_lock);
    diagnostics.receive_missed_count = status.rx_missed_count;
    diagnostics.bus_error_count = status.bus_error_count;
    diagnostics.rx_error_counter = status.rx_error_counter;
    portEXIT_CRITICAL(&state_lock);
}

static esp_err_t start_driver(uint32_t bitrate)
{
    const twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(
        CLUSTER_CENTER_CAN_TX_GPIO, CLUSTER_CENTER_CAN_RX_GPIO, TWAI_MODE_NORMAL);
    const twai_timing_config_t timing = timing_for_bitrate(bitrate);
    const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t result = twai_driver_install(&general, &timing, &filter);
    if (result != ESP_OK) {
        return result;
    }
    result = twai_start();
    if (result != ESP_OK) {
        twai_driver_uninstall();
    }
    return result;
}

static void stop_driver(void)
{
    esp_err_t result = twai_stop();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "TWAI stop failed: %s", esp_err_to_name(result));
    }
    result = twai_driver_uninstall();
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "TWAI uninstall failed: %s", esp_err_to_name(result));
    }
}

static bool probe_bitrate(uint32_t bitrate)
{
    if (start_driver(bitrate) != ESP_OK) {
        return false;
    }

    const uint32_t probe_start_ms = now_ms();
    unsigned valid_frames = 0U;
    while ((uint32_t)(now_ms() - probe_start_ms) < HONDATA_CAN_PROBE_TIME_MS) {
        twai_message_t message;
        if (twai_receive(&message, pdMS_TO_TICKS(20)) == ESP_OK &&
            process_message(&message)) {
            valid_frames++;
            if (valid_frames >= 2U) {
                ESP_LOGI(TAG, "Hondata CAN detected at %" PRIu32 " bit/s", bitrate);
                set_controller_state(HONDATA_CAN_RUNNING, bitrate);
                return true;
            }
        }
    }

    stop_driver();
    return false;
}

static bool recover_bus_off(uint32_t bitrate)
{
    set_controller_state(HONDATA_CAN_BUS_OFF, bitrate);
    esp_err_t result = twai_initiate_recovery();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "TWAI recovery start failed: %s", esp_err_to_name(result));
        return false;
    }

    set_controller_state(HONDATA_CAN_RECOVERING, bitrate);
    for (unsigned attempt = 0; attempt < 50U; ++attempt) {
        twai_status_info_t status;
        if (twai_get_status_info(&status) == ESP_OK &&
            status.state == TWAI_STATE_STOPPED) {
            result = twai_start();
            if (result == ESP_OK) {
                set_controller_state(HONDATA_CAN_RUNNING, bitrate);
                return true;
            }
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}

static void receive_until_stale(uint32_t bitrate)
{
    for (;;) {
        twai_message_t message;
        if (twai_receive(&message, pdMS_TO_TICKS(20)) == ESP_OK) {
            process_message(&message);
        }

        twai_status_info_t status;
        if (twai_get_status_info(&status) == ESP_OK &&
            status.state == TWAI_STATE_BUS_OFF && !recover_bus_off(bitrate)) {
            break;
        }
        refresh_driver_diagnostics();

        uint32_t last_frame;
        portENTER_CRITICAL(&state_lock);
        last_frame = last_valid_frame_ms;
        portEXIT_CRITICAL(&state_lock);
        if (last_frame == 0U ||
            (uint32_t)(now_ms() - last_frame) > HONDATA_CAN_RESCAN_TIMEOUT_MS) {
            ESP_LOGW(TAG, "Hondata traffic stale; rescanning bitrate");
            break;
        }
    }
}

static void hondata_can_task(void *argument)
{
    (void)argument;
    unsigned scan_pass = 0U;

    for (;;) {
        if (scan_pass == 0U || scan_pass % 12U == 0U) {
            ESP_LOGI(TAG, "Scanning Hondata CAN bitrates");
        }
        scan_pass++;
        set_controller_state(HONDATA_CAN_SCANNING, 0U);
        bool detected = false;
        for (size_t index = 0;
             index < sizeof(can_bitrates) / sizeof(can_bitrates[0]); ++index) {
            const uint32_t bitrate = can_bitrates[index];
            if (probe_bitrate(bitrate)) {
                detected = true;
                receive_until_stale(bitrate);
                stop_driver();
                break;
            }
        }
        if (!detected) {
            set_controller_state(HONDATA_CAN_OFFLINE, 0U);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

esp_err_t hondata_can_start(void)
{
    if (can_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    hondata_state_reset(&decoded_state);
    memset(&diagnostics, 0, sizeof(diagnostics));
    last_valid_frame_ms = 0U;

    const BaseType_t result = xTaskCreate(
        hondata_can_task, "hondata_can", HONDATA_CAN_TASK_STACK_SIZE,
        NULL, HONDATA_CAN_TASK_PRIORITY, &can_task_handle);
    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void hondata_can_get_state(hondata_state_t *state)
{
    if (state == NULL) {
        return;
    }
    portENTER_CRITICAL(&state_lock);
    *state = decoded_state;
    portEXIT_CRITICAL(&state_lock);
}

void hondata_can_get_vehicle_fast(vehicle_fast_state_t *vehicle)
{
    if (vehicle == NULL) {
        return;
    }

    hondata_state_t state;
    hondata_can_get_state(&state);
    const uint32_t current_time_ms = now_ms();
    for (size_t index = 0; index < HONDATA_CAN_FRAME_COUNT; ++index) {
        if ((state.received_frame_mask & (1UL << index)) != 0U &&
            (uint32_t)(current_time_ms - state.last_frame_time_ms[index]) >
                HONDATA_CAN_LIVE_TIMEOUT_MS) {
            state.received_frame_mask &= ~(1UL << index);
        }
    }

    memset(vehicle, 0, sizeof(*vehicle));
    vehicle->sample_time_ms = current_time_ms;
    hondata_apply_vehicle_fast(&state, vehicle);
}

void hondata_can_get_diagnostics(hondata_can_diagnostics_t *output)
{
    if (output == NULL) {
        return;
    }

    portENTER_CRITICAL(&state_lock);
    *output = diagnostics;
    const uint32_t last_frame = last_valid_frame_ms;
    portEXIT_CRITICAL(&state_lock);

    if (last_frame != 0U) {
        output->last_frame_age_ms = (uint32_t)(now_ms() - last_frame);
        output->live_data = output->last_frame_age_ms <= HONDATA_CAN_LIVE_TIMEOUT_MS;
    }
}