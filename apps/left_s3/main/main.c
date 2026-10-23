#include "climate_link.h"
#include "cluster_board_config.h"
#include "cluster_protocol.h"
#include "cluster_secrets.h"
#include "cluster_wireless.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "left_dashboard.h"
#include "side_display.h"

static const char *TAG = "triple_left";
static const uint8_t climate_gateway_mac[6] = CLIMATE_GATEWAY_MAC;

/* The left display is the climate gateway: it forwards the P4's target
 * positions to the climate node and reports the node's state back. */
static void climate_state_changed(const cluster_climate_state_t *state,
                                  void *context)
{
    (void)context;
    uint8_t payload[CLUSTER_CLIMATE_STATE_PAYLOAD_SIZE];
    if (cluster_climate_state_encode(state, payload, sizeof(payload)) ==
        CLUSTER_PROTOCOL_OK) {
        cluster_wireless_send_message(CLUSTER_WIRELESS_ROLE_CENTER,
                                      CLUSTER_MSG_CLIMATE_STATE, payload,
                                      sizeof(payload));
    }
}

static void handle_message(const cluster_frame_view_t *view)
{
    switch (view->type) {
    case CLUSTER_MSG_VEHICLE_FAST: {
        cluster_vehicle_fast_t vehicle;
        if (cluster_vehicle_fast_decode(view->payload, view->payload_size,
                                        &vehicle) == CLUSTER_PROTOCOL_OK) {
            left_dashboard_update(&vehicle);
        }
        break;
    }
    case CLUSTER_MSG_SETTINGS: {
        cluster_left_settings_t settings;
        if (cluster_left_settings_decode(view->payload, view->payload_size,
                                         &settings) == CLUSTER_PROTOCOL_OK) {
            left_dashboard_apply_settings(&settings);
        }
        break;
    }
    case CLUSTER_MSG_DISPLAY_MODE: {
        cluster_display_mode_t mode;
        if (cluster_display_mode_decode(view->payload, view->payload_size,
                                        &mode) == CLUSTER_PROTOCOL_OK &&
            mode.target == CLUSTER_DISPLAY_LEFT) {
            left_dashboard_apply_display_mode(mode.view);
        }
        break;
    }
    case CLUSTER_MSG_CLIMATE_COMMAND: {
        cluster_climate_command_t command;
        if (cluster_climate_command_decode(view->payload, view->payload_size,
                                           &command) == CLUSTER_PROTOCOL_OK) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(climate_link_set(&command));
        }
        break;
    }
    default:
        break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "MackoDash Triple left S3 v0.1.0");
    ESP_LOGI(TAG, "Role: display-only wireless node");
    ESP_LOGI(TAG, "Cluster protocol self-test: %s",
             cluster_protocol_self_test() ? "PASS" : "FAIL");

    ESP_ERROR_CHECK(side_display_start(left_dashboard_create, NULL, true));

    const cluster_wireless_config_t wireless_config = {
        .role = CLUSTER_WIRELESS_ROLE_LEFT,
        .initialize_wifi = true,
        .connect_to_center = true,
        .station_mac = climate_gateway_mac,
    };
    ESP_ERROR_CHECK(cluster_wireless_start(&wireless_config));
    ESP_LOGI(TAG, "Joining MackoDash wireless network on channel %u",
             CLUSTER_WIRELESS_CHANNEL);

    uint8_t frame[CLUSTER_PROTOCOL_MAX_FRAME_SIZE];
    int64_t last_heartbeat_us = esp_timer_get_time();
    bool center_online = false;
    bool climate_started = false;

    for (;;) {
        /* The climate link needs the station running on the cluster channel. */
        if (!climate_started && cluster_wireless_ready()) {
            climate_started = true;
            ESP_ERROR_CHECK_WITHOUT_ABORT(
                climate_link_start(climate_state_changed, NULL));
        }
        size_t frame_size = 0U;
        cluster_frame_view_t view;
        if (cluster_wireless_receive_message(frame, sizeof(frame), &frame_size,
                                             &view, pdMS_TO_TICKS(250)) ==
            ESP_OK) {
            if (view.type == CLUSTER_MSG_HEARTBEAT) {
                last_heartbeat_us = esp_timer_get_time();
                if (!center_online) left_dashboard_set_offline(false);
                center_online = true;
            } else {
                handle_message(&view);
            }
        }
        if (center_online &&
            esp_timer_get_time() - last_heartbeat_us >=
                CLUSTER_HEARTBEAT_TIMEOUT_MS * 1000LL) {
            ESP_LOGW(TAG, "Center heartbeat lost");
            left_dashboard_set_offline(true);
            center_online = false;
        }
    }
}
