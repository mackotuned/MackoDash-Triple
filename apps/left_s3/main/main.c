#include "climate_link.h"
#include "cluster_board_config.h"
#include "cluster_ota.h"
#include "cluster_protocol.h"
#include "cluster_secrets.h"
#include "cluster_wireless.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "side_display.h"
#include "side_pages.h"

/* Air Ride reports are relayed by the P4 every 100 ms. */
#define AIR_STATE_TIMEOUT_US 500000LL

static const char *TAG = "triple_left";
static const uint8_t climate_gateway_mac[6] = CLIMATE_GATEWAY_MAC;

/* The right display owns the Air Ride radio; this display mirrors its
 * reports and sends commands through the P4, which forwards them. */
static portMUX_TYPE air_lock = portMUX_INITIALIZER_UNLOCKED;
static cluster_air_ride_state_t air_state;
static int64_t air_received_us;

static void air_get_state(side_air_state_t *state)
{
    cluster_air_ride_state_t copy;
    int64_t received_us;
    taskENTER_CRITICAL(&air_lock);
    copy = air_state;
    received_us = air_received_us;
    taskEXIT_CRITICAL(&air_lock);
    const bool fresh = received_us != 0 &&
        esp_timer_get_time() - received_us < AIR_STATE_TIMEOUT_US;
    *state = (side_air_state_t){
        .connected = fresh && copy.connected,
        .pressure_valid = fresh && copy.pressure_valid,
        .compressor_valid = fresh && copy.compressor_valid,
        .compressor_on = copy.compressor_on,
        .psi = {
            [SIDE_AIR_FRONT_LEFT] = copy.front_left_psi,
            [SIDE_AIR_FRONT_RIGHT] = copy.front_right_psi,
            [SIDE_AIR_REAR_LEFT] = copy.rear_left_psi,
            [SIDE_AIR_REAR_RIGHT] = copy.rear_right_psi,
            [SIDE_AIR_TANK] = copy.tank_psi,
        },
    };
}

static void air_command(cluster_air_ride_command_code_t code)
{
    const cluster_air_ride_command_t command = {.code = code};
    uint8_t payload[CLUSTER_AIR_RIDE_COMMAND_PAYLOAD_SIZE];
    if (cluster_air_ride_command_encode(&command, payload, sizeof(payload)) ==
        CLUSTER_PROTOCOL_OK) {
        cluster_wireless_send_message(CLUSTER_WIRELESS_ROLE_CENTER,
                                      CLUSTER_MSG_AIR_RIDE_COMMAND, payload,
                                      sizeof(payload));
    }
}

static const side_air_backend_t air_backend = {
    .get_state = air_get_state,
    .command = air_command,
};
static const side_pages_config_t pages_config = {
    .display = CLUSTER_DISPLAY_LEFT,
    .first_view = CLUSTER_VIEW_DATA,
    .air = &air_backend,
};

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
            side_pages_update_vehicle(&vehicle);
        }
        break;
    }
    case CLUSTER_MSG_SETTINGS: {
        cluster_left_settings_t settings;
        if (cluster_left_settings_decode(view->payload, view->payload_size,
                                         &settings) == CLUSTER_PROTOCOL_OK) {
            side_pages_apply_settings(&settings);
        }
        break;
    }
    case CLUSTER_MSG_DISPLAY_MODE: {
        cluster_display_mode_t mode;
        if (cluster_display_mode_decode(view->payload, view->payload_size,
                                        &mode) == CLUSTER_PROTOCOL_OK &&
            mode.target == CLUSTER_DISPLAY_LEFT) {
            side_pages_show_view(mode.view);
        }
        break;
    }
    case CLUSTER_MSG_AIR_RIDE_STATE: {
        cluster_air_ride_state_t state;
        if (cluster_air_ride_state_decode(view->payload, view->payload_size,
                                          &state) == CLUSTER_PROTOCOL_OK) {
            taskENTER_CRITICAL(&air_lock);
            air_state = state;
            air_received_us = esp_timer_get_time();
            taskEXIT_CRITICAL(&air_lock);
        }
        break;
    }
    case CLUSTER_MSG_OTA_OFFER: {
        /* Climate node updates come through here: the node joins the
         * network just long enough to download its image. */
        cluster_ota_offer_t offer;
        if (cluster_ota_offer_decode(view->payload, view->payload_size,
                                     &offer) != CLUSTER_PROTOCOL_OK) {
            break;
        }
        if (offer.target == CLUSTER_OTA_LEFT) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(cluster_ota_start(CLUSTER_OTA_LEFT));
        } else if (offer.target == CLUSTER_OTA_CLIMATE) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(
                climate_link_request_update(offer.image_size));
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
    ESP_LOGI(TAG, "Role: side display and climate gateway");
    ESP_LOGI(TAG, "Cluster protocol self-test: %s",
             cluster_protocol_self_test() ? "PASS" : "FAIL");

    ESP_ERROR_CHECK(side_display_start(side_pages_create,
                                       (void *)&pages_config, true));

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
            /* Rejoining the network proves a new firmware; keep it. */
            cluster_ota_mark_valid();
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
                if (!center_online) side_pages_set_center_online(true);
                center_online = true;
            } else {
                handle_message(&view);
            }
        }
        if (center_online &&
            esp_timer_get_time() - last_heartbeat_us >=
                CLUSTER_HEARTBEAT_TIMEOUT_MS * 1000LL) {
            ESP_LOGW(TAG, "Center heartbeat lost");
            side_pages_set_center_online(false);
            center_online = false;
        }
    }
}
