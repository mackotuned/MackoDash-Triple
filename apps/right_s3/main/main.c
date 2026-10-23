#include "air_ride_gateway.h"
#include "cluster_board_config.h"
#include "cluster_protocol.h"
#include "cluster_wireless.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "right_ui.h"
#include "side_display.h"
#include "side_speedometer.h"
#include "vehicle_data.h"

#define AIR_STATE_PERIOD_MS 100U
/* A remote movement command is released unless the sender repeats it. */
#define REMOTE_COMMAND_TIMEOUT_US 500000LL

static const char *TAG = "triple_right";
static bool remote_command_active;
static int64_t remote_command_deadline_us;

static uint8_t target_for_air_command(cluster_air_ride_command_code_t command)
{
    switch (command) {
    case CLUSTER_AIR_RIDE_FRONT_UP:
    case CLUSTER_AIR_RIDE_FRONT_DOWN: return CLUSTER_AIR_TARGET_FRONT;
    case CLUSTER_AIR_RIDE_REAR_UP:
    case CLUSTER_AIR_RIDE_REAR_DOWN: return CLUSTER_AIR_TARGET_REAR;
    case CLUSTER_AIR_RIDE_FRONT_LEFT_UP:
    case CLUSTER_AIR_RIDE_FRONT_LEFT_DOWN: return CLUSTER_AIR_TARGET_FRONT_LEFT;
    case CLUSTER_AIR_RIDE_FRONT_RIGHT_UP:
    case CLUSTER_AIR_RIDE_FRONT_RIGHT_DOWN: return CLUSTER_AIR_TARGET_FRONT_RIGHT;
    case CLUSTER_AIR_RIDE_REAR_LEFT_UP:
    case CLUSTER_AIR_RIDE_REAR_LEFT_DOWN: return CLUSTER_AIR_TARGET_REAR_LEFT;
    case CLUSTER_AIR_RIDE_REAR_RIGHT_UP:
    case CLUSTER_AIR_RIDE_REAR_RIGHT_DOWN: return CLUSTER_AIR_TARGET_REAR_RIGHT;
    default: return CLUSTER_AIR_TARGET_ALL;
    }
}

static void handle_air_command(const cluster_frame_view_t *view)
{
    static const air_ride_command_t manual_commands[] = {
        [CLUSTER_AIR_RIDE_FRONT_LEFT_UP] = AIR_RIDE_FRONT_LEFT_UP,
        [CLUSTER_AIR_RIDE_FRONT_LEFT_DOWN] = AIR_RIDE_FRONT_LEFT_DOWN,
        [CLUSTER_AIR_RIDE_FRONT_RIGHT_UP] = AIR_RIDE_FRONT_RIGHT_UP,
        [CLUSTER_AIR_RIDE_FRONT_RIGHT_DOWN] = AIR_RIDE_FRONT_RIGHT_DOWN,
        [CLUSTER_AIR_RIDE_REAR_LEFT_UP] = AIR_RIDE_REAR_LEFT_UP,
        [CLUSTER_AIR_RIDE_REAR_LEFT_DOWN] = AIR_RIDE_REAR_LEFT_DOWN,
        [CLUSTER_AIR_RIDE_REAR_RIGHT_UP] = AIR_RIDE_REAR_RIGHT_UP,
        [CLUSTER_AIR_RIDE_REAR_RIGHT_DOWN] = AIR_RIDE_REAR_RIGHT_DOWN,
        [CLUSTER_AIR_RIDE_ALL_UP] = AIR_RIDE_ALL_UP,
        [CLUSTER_AIR_RIDE_ALL_DOWN] = AIR_RIDE_ALL_DOWN,
        [CLUSTER_AIR_RIDE_RELEASE] = AIR_RIDE_RELEASE,
        [CLUSTER_AIR_RIDE_FRONT_UP] = AIR_RIDE_FRONT_UP,
        [CLUSTER_AIR_RIDE_FRONT_DOWN] = AIR_RIDE_FRONT_DOWN,
        [CLUSTER_AIR_RIDE_REAR_UP] = AIR_RIDE_REAR_UP,
        [CLUSTER_AIR_RIDE_REAR_DOWN] = AIR_RIDE_REAR_DOWN,
    };

    cluster_air_ride_command_t command;
    if (cluster_air_ride_command_decode(view->payload, view->payload_size,
                                        &command) != CLUSTER_PROTOCOL_OK) {
        ESP_LOGW(TAG, "Rejected malformed Air Ride command");
        return;
    }
    if (!right_ui_controls_armed()) {
        ESP_LOGW(TAG, "Rejected Air Ride command while controls are disarmed");
        return;
    }

    esp_err_t error;
    if (command.code == CLUSTER_AIR_RIDE_COMPRESSOR_OFF ||
        command.code == CLUSTER_AIR_RIDE_COMPRESSOR_ON) {
        error = air_ride_gateway_set_compressor(
            command.code == CLUSTER_AIR_RIDE_COMPRESSOR_ON,
            command.compressor_on_psi, command.compressor_off_psi);
    } else {
        const air_ride_command_t manual = manual_commands[command.code];
        error = air_ride_gateway_command(manual);
        remote_command_active = manual != AIR_RIDE_RELEASE;
        remote_command_deadline_us =
            esp_timer_get_time() + REMOTE_COMMAND_TIMEOUT_US;
        right_ui_remote_adjustment(remote_command_active,
                                   target_for_air_command(command.code));
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Air Ride command failed: %s", esp_err_to_name(error));
    }
}

static void send_air_state(void)
{
    air_ride_state_t gateway;
    air_ride_gateway_get_state(&gateway);
    cluster_air_ride_state_t state = {
        .connected = gateway.connected,
        .pressure_valid = gateway.pressure_valid,
        .compressor_valid = gateway.compressor_valid,
        .compressor_on = gateway.compressor_on,
        .tank_psi = gateway.tank_psi,
        .front_left_psi = gateway.front_left_psi,
        .front_right_psi = gateway.front_right_psi,
        .rear_left_psi = gateway.rear_left_psi,
        .rear_right_psi = gateway.rear_right_psi,
    };
    right_ui_get_adjustment(&state.adjustment_open, &state.adjustment_target);
    uint8_t payload[CLUSTER_AIR_RIDE_STATE_PAYLOAD_SIZE];
    if (cluster_air_ride_state_encode(&state, payload, sizeof(payload)) ==
        CLUSTER_PROTOCOL_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(cluster_wireless_send_message(
            CLUSTER_WIRELESS_ROLE_CENTER, CLUSTER_MSG_AIR_RIDE_STATE, payload,
            sizeof(payload)));
    }
}

static void handle_message(const cluster_frame_view_t *view)
{
    switch (view->type) {
    case CLUSTER_MSG_VEHICLE_FAST: {
        cluster_vehicle_fast_t vehicle;
        if (cluster_vehicle_fast_decode(view->payload, view->payload_size,
                                        &vehicle) == CLUSTER_PROTOCOL_OK) {
            side_speedometer_update(
                vehicle.speed_tenths_kph,
                vehicle_data_channel_valid(vehicle.valid_channels,
                                           VEHICLE_DATA_SPEED));
        }
        break;
    }
    case CLUSTER_MSG_AIR_RIDE_COMMAND:
        handle_air_command(view);
        break;
    case CLUSTER_MSG_DISPLAY_MODE: {
        cluster_display_mode_t mode;
        if (cluster_display_mode_decode(view->payload, view->payload_size,
                                        &mode) == CLUSTER_PROTOCOL_OK &&
            mode.target == CLUSTER_DISPLAY_RIGHT) {
            right_ui_show_view(mode.view);
        }
        break;
    }
    case CLUSTER_MSG_SETTINGS: {
        cluster_left_settings_t settings;
        if (cluster_left_settings_decode(view->payload, view->payload_size,
                                         &settings) == CLUSTER_PROTOCOL_OK) {
            right_ui_apply_settings(&settings);
        }
        break;
    }
    default:
        break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "MackoDash Triple right S3 v0.1.0");
    ESP_LOGI(TAG, "Role: Air Ride display and gateway");
    ESP_LOGI(TAG, "Cluster protocol self-test: %s",
             cluster_protocol_self_test() ? "PASS" : "FAIL");

    ESP_ERROR_CHECK(side_display_start(right_ui_create, NULL, true));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(air_ride_gateway_start(NULL, NULL));
    const cluster_wireless_config_t wireless_config = {
        .role = CLUSTER_WIRELESS_ROLE_RIGHT,
        .initialize_wifi = false,
        .connect_to_center = true,
    };
    ESP_ERROR_CHECK(cluster_wireless_start(&wireless_config));
    ESP_LOGI(TAG, "Joining MackoDash wireless network on channel %u",
             CLUSTER_WIRELESS_CHANNEL);

    uint8_t frame[CLUSTER_PROTOCOL_MAX_FRAME_SIZE];
    int64_t last_heartbeat_us = esp_timer_get_time();
    int64_t last_state_us = 0;
    bool center_online = false;

    for (;;) {
        size_t frame_size = 0U;
        cluster_frame_view_t view;
        if (cluster_wireless_receive_message(frame, sizeof(frame), &frame_size,
                                             &view, pdMS_TO_TICKS(20)) ==
            ESP_OK) {
            if (view.type == CLUSTER_MSG_HEARTBEAT) {
                last_heartbeat_us = esp_timer_get_time();
                if (!center_online) right_ui_set_center_online(true);
                center_online = true;
            } else {
                handle_message(&view);
            }
        }

        const int64_t now_us = esp_timer_get_time();
        if (remote_command_active && now_us >= remote_command_deadline_us) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(
                air_ride_gateway_command(AIR_RIDE_RELEASE));
            remote_command_active = false;
        }
        if (now_us - last_state_us >= AIR_STATE_PERIOD_MS * 1000LL) {
            last_state_us = now_us;
            if (cluster_wireless_ready()) send_air_state();
        }
        if (center_online &&
            now_us - last_heartbeat_us >= CLUSTER_HEARTBEAT_TIMEOUT_MS * 1000LL) {
            ESP_LOGW(TAG, "Center heartbeat lost");
            right_ui_set_center_online(false);
            center_online = false;
        }
    }
}
