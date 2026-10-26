#include "center_link.h"

#include <inttypes.h>
#include <stdatomic.h>

#include "air_link.h"
#include "center_display.h"
#include "cluster_wireless.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hondata_logger.h"
#include "settings_service.h"

static const char *TAG = "center_link";

/* Last view selected for each display, and the views to restore when the
 * knob leaves its Air Ride page. Only touched by the receive task. */
static cluster_display_view_t display_views[CLUSTER_DISPLAY_COUNT] = {
    [CLUSTER_DISPLAY_LEFT] = CLUSTER_VIEW_DATA,
    [CLUSTER_DISPLAY_CENTER] = CLUSTER_VIEW_TACHOMETER,
    [CLUSTER_DISPLAY_RIGHT] = CLUSTER_VIEW_AIR_RIDE,
};
static cluster_display_view_t saved_center_view = CLUSTER_VIEW_TACHOMETER;
static cluster_display_view_t saved_right_view = CLUSTER_VIEW_AIR_RIDE;
static bool knob_air_view_open;

/* Climate dial targets. Until the knob changes something, the P4 adopts the
 * targets the node reports so it never overrides the car on boot. */
static portMUX_TYPE climate_lock = portMUX_INITIALIZER_UNLOCKED;
static cluster_climate_command_t climate_target = {.fan = 0, .mode = 0,
                                                   .temp = 7};
static bool climate_target_owned;
static cluster_climate_state_t climate_reported;
static int64_t climate_reported_us;

static uint32_t uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool display_mode_supported(cluster_display_target_t target,
                                   cluster_display_view_t view)
{
    switch (target) {
    case CLUSTER_DISPLAY_CENTER:
        return view == CLUSTER_VIEW_TACHOMETER ||
               view == CLUSTER_VIEW_AIR_RIDE || view == CLUSTER_VIEW_LOGS;
    case CLUSTER_DISPLAY_LEFT:
    case CLUSTER_DISPLAY_RIGHT:
        return view == CLUSTER_VIEW_DATA || view == CLUSTER_VIEW_SPEEDOMETER ||
               view == CLUSTER_VIEW_AIR_RIDE || view == CLUSTER_VIEW_CLIMATE ||
               view == CLUSTER_VIEW_GAUGES || view == CLUSTER_VIEW_PEAKS ||
               view == CLUSTER_VIEW_TIMER;
    default:
        return false;
    }
}

static esp_err_t apply_display_mode(cluster_display_target_t target,
                                    cluster_display_view_t view)
{
    if (!display_mode_supported(target, view)) return ESP_ERR_NOT_SUPPORTED;
    display_views[target] = view;
    if (target == CLUSTER_DISPLAY_CENTER) {
        center_display_show_view(view);
        return ESP_OK;
    }

    const cluster_display_mode_t mode = {.target = target, .view = view};
    uint8_t payload[CLUSTER_DISPLAY_MODE_PAYLOAD_SIZE];
    if (cluster_display_mode_encode(&mode, payload, sizeof(payload)) !=
        CLUSTER_PROTOCOL_OK) {
        return ESP_FAIL;
    }
    return cluster_wireless_send_message(
        target == CLUSTER_DISPLAY_LEFT ? CLUSTER_WIRELESS_ROLE_LEFT
                                       : CLUSTER_WIRELESS_ROLE_RIGHT,
        CLUSTER_MSG_DISPLAY_MODE, payload, sizeof(payload));
}

/* The knob's Air Ride page takes over the center and right displays and
 * hands them back to their previous views when it closes. */
static void set_knob_air_view(bool open)
{
    if (open == knob_air_view_open) return;
    knob_air_view_open = open;
    if (open) {
        saved_center_view = display_views[CLUSTER_DISPLAY_CENTER];
        saved_right_view = display_views[CLUSTER_DISPLAY_RIGHT];
        ESP_ERROR_CHECK_WITHOUT_ABORT(apply_display_mode(
            CLUSTER_DISPLAY_CENTER, CLUSTER_VIEW_AIR_RIDE));
        ESP_ERROR_CHECK_WITHOUT_ABORT(apply_display_mode(
            CLUSTER_DISPLAY_RIGHT, CLUSTER_VIEW_AIR_RIDE));
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            air_link_send_command(CLUSTER_AIR_RIDE_RELEASE));
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            apply_display_mode(CLUSTER_DISPLAY_CENTER, saved_center_view));
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            apply_display_mode(CLUSTER_DISPLAY_RIGHT, saved_right_view));
    }
}

static esp_err_t send_climate_target(void)
{
    cluster_climate_command_t target;
    bool owned;
    taskENTER_CRITICAL(&climate_lock);
    target = climate_target;
    owned = climate_target_owned;
    taskEXIT_CRITICAL(&climate_lock);
    if (!owned) return ESP_OK;

    uint8_t payload[CLUSTER_CLIMATE_COMMAND_PAYLOAD_SIZE];
    if (cluster_climate_command_encode(&target, payload, sizeof(payload)) !=
        CLUSTER_PROTOCOL_OK) {
        return ESP_FAIL;
    }
    return cluster_wireless_send_message(CLUSTER_WIRELESS_ROLE_LEFT,
                                         CLUSTER_MSG_CLIMATE_COMMAND, payload,
                                         sizeof(payload));
}

/* Sets one dial target and forwards it; shared by the knob and the phone. */
static esp_err_t set_climate_dial(settings_climate_dial_t dial, uint32_t value)
{
    static const uint32_t maximums[] = {
        [SETTINGS_CLIMATE_FAN] = CLUSTER_CLIMATE_FAN_MAX,
        [SETTINGS_CLIMATE_TEMP] = CLUSTER_CLIMATE_TEMP_MAX,
        [SETTINGS_CLIMATE_MODE] = CLUSTER_CLIMATE_MODE_MAX,
    };
    if (value > maximums[dial]) return ESP_ERR_INVALID_ARG;
    taskENTER_CRITICAL(&climate_lock);
    switch (dial) {
    case SETTINGS_CLIMATE_FAN: climate_target.fan = (uint8_t)value; break;
    case SETTINGS_CLIMATE_TEMP: climate_target.temp = (uint8_t)value; break;
    default: climate_target.mode = (uint8_t)value; break;
    }
    climate_target_owned = true;
    taskEXIT_CRITICAL(&climate_lock);
    return send_climate_target();
}

/* Knob climate commands carry the dial position directly. */
static bool handle_climate_command(const cluster_knob_command_t *command)
{
    if (command->target != 0U || command->value < 0) return false;
    const settings_climate_dial_t dial =
        command->code == CLUSTER_KNOB_HVAC_FAN_SET ? SETTINGS_CLIMATE_FAN
        : command->code == CLUSTER_KNOB_HVAC_BLEND_SET ? SETTINGS_CLIMATE_TEMP
                                                       : SETTINGS_CLIMATE_MODE;
    const esp_err_t error = set_climate_dial(dial, (uint32_t)command->value);
    if (error == ESP_ERR_INVALID_ARG) return false;
    ESP_ERROR_CHECK_WITHOUT_ABORT(error);
    return true;
}

/* Node reports arrive about every 500 ms. */
static bool climate_online(cluster_climate_state_t *state)
{
    taskENTER_CRITICAL(&climate_lock);
    *state = climate_reported;
    const int64_t reported_us = climate_reported_us;
    taskEXIT_CRITICAL(&climate_lock);
    return state->connected && reported_us != 0 &&
           esp_timer_get_time() - reported_us < 2000000LL;
}

static esp_err_t web_climate_command(settings_climate_dial_t dial,
                                     uint32_t value)
{
    cluster_climate_state_t state;
    if (!climate_online(&state)) return ESP_ERR_INVALID_STATE;
    const esp_err_t error = set_climate_dial(dial, value);
    return error == ESP_ERR_INVALID_ARG ? error
           : error == ESP_OK            ? ESP_OK
                                        : ESP_ERR_INVALID_STATE;
}

static void handle_climate_state(const cluster_frame_view_t *view,
                                  const uint8_t *frame, size_t frame_size)
{
    cluster_climate_state_t state;
    if (cluster_climate_state_decode(view->payload, view->payload_size,
                                     &state) != CLUSTER_PROTOCOL_OK) {
        return;
    }
    taskENTER_CRITICAL(&climate_lock);
    climate_reported = state;
    climate_reported_us = esp_timer_get_time();
    if (!climate_target_owned && state.connected) {
        climate_target = (cluster_climate_command_t){
            .fan = state.target_fan,
            .mode = state.target_mode,
            .temp = state.target_temp,
        };
    }
    taskEXIT_CRITICAL(&climate_lock);
    /* The knob shows the node's positions; relay the report unchanged. */
    ESP_ERROR_CHECK_WITHOUT_ABORT(cluster_wireless_send(
        CLUSTER_WIRELESS_ROLE_KNOB, frame, frame_size));
}

static esp_err_t send_logging_status(void)
{
    hondata_logger_status_t logger;
    hondata_logger_get_status(&logger);
    const cluster_logging_status_t status = {
        .available = logger.mounted,
        .recording = logger.recording,
        .session_index = logger.session_index,
        .record_count = logger.record_count,
        .dropped_count = logger.dropped_count,
    };
    uint8_t payload[CLUSTER_LOGGING_STATUS_PAYLOAD_SIZE];
    if (cluster_logging_status_encode(&status, payload, sizeof(payload)) !=
        CLUSTER_PROTOCOL_OK) {
        return ESP_FAIL;
    }
    return cluster_wireless_send_message(CLUSTER_WIRELESS_ROLE_KNOB,
                                         CLUSTER_MSG_LOGGING_STATUS, payload,
                                         sizeof(payload));
}

/* Live air adjustments move the car toward the knob value, one burst per
 * command; confirm or cancel releases the valves. */
static void handle_air_adjustment(const cluster_knob_command_t *command)
{
    center_display_close_logs();
    set_knob_air_view(true);
    air_link_set_target(command->target);
    if ((command->flags & (CLUSTER_KNOB_COMMAND_CANCEL |
                           CLUSTER_KNOB_COMMAND_CONFIRM)) != 0U) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            air_link_send_command(CLUSTER_AIR_RIDE_RELEASE));
        return;
    }
    if ((command->flags & CLUSTER_KNOB_COMMAND_LIVE) == 0U) return;

    air_link_state_t air;
    air_link_get(&air);
    if (!air.connected || !air.pressure_valid) return;
    const uint16_t current = air_link_target_pressure(&air, command->target);
    if (command->value >= 0 && command->value <= 200 &&
        command->value != current) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(air_link_send_command(
            cluster_air_target_command(command->target,
                                    command->value > current)));
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            air_link_send_command(CLUSTER_AIR_RIDE_RELEASE));
    }
}

static void handle_knob_command(const cluster_frame_view_t *view)
{
    cluster_knob_command_t command;
    if (cluster_knob_command_decode(view->payload, view->payload_size,
                                    &command) != CLUSTER_PROTOCOL_OK) {
        return;
    }
    const bool confirmed = (command.flags & CLUSTER_KNOB_COMMAND_CONFIRM) != 0U;
    const bool boolean_value = command.value == 0 || command.value == 1;

    switch (command.code) {
    case CLUSTER_KNOB_BRIGHTNESS_SET:
        if (command.target == 0U && command.value >= 5 &&
            command.value <= 100) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(settings_service_set_all_brightness(
                (uint8_t)command.value, confirmed));
            return;
        }
        break;
    case CLUSTER_KNOB_RECORD_SET:
        if (command.target == 0U && confirmed && boolean_value) {
            cluster_settings_t settings;
            settings_service_get(&settings);
            ESP_ERROR_CHECK_WITHOUT_ABORT(hondata_logger_set_recording(
                command.value != 0, (uint8_t)settings.logging.log_name,
                uptime_ms()));
            ESP_ERROR_CHECK_WITHOUT_ABORT(send_logging_status());
            return;
        }
        break;
    case CLUSTER_KNOB_LOGS_OPEN:
        if (command.target < CLUSTER_DISPLAY_COUNT && confirmed) {
            if (apply_display_mode((cluster_display_target_t)command.target,
                                   CLUSTER_VIEW_LOGS) != ESP_OK) {
                ESP_LOGW(TAG, "Logs view is unavailable on display %u",
                         (unsigned)command.target);
            }
            return;
        }
        break;
    case CLUSTER_KNOB_SCREEN_SET:
        if (command.target < CLUSTER_DISPLAY_COUNT && command.value >= 0 &&
            command.value < CLUSTER_VIEW_COUNT && confirmed) {
            if (apply_display_mode((cluster_display_target_t)command.target,
                                   (cluster_display_view_t)command.value) !=
                ESP_OK) {
                ESP_LOGW(TAG, "View %" PRId32 " is unavailable on display %u",
                         command.value, (unsigned)command.target);
            }
            return;
        }
        break;
    case CLUSTER_KNOB_AIR_VIEW_SET:
        if (command.target == 0U && confirmed && boolean_value) {
            set_knob_air_view(command.value != 0);
            return;
        }
        break;
    case CLUSTER_KNOB_HVAC_FAN_SET:
    case CLUSTER_KNOB_HVAC_BLEND_SET:
    case CLUSTER_KNOB_HVAC_MODE_SET:
        if (handle_climate_command(&command)) return;
        break;
    case CLUSTER_KNOB_AIR_SET:
        if (command.target < CLUSTER_AIR_TARGET_COUNT) {
            handle_air_adjustment(&command);
            return;
        }
        break;
    default:
        break;
    }
    if ((command.flags & CLUSTER_KNOB_COMMAND_LIVE) == 0U) {
        ESP_LOGW(TAG, "Knob command %u is not supported by this backend",
                 (unsigned)command.code);
    }
}

static void receive_task(void *argument)
{
    (void)argument;
    uint8_t frame[CLUSTER_PROTOCOL_MAX_FRAME_SIZE];
    for (;;) {
        size_t frame_size = 0U;
        cluster_frame_view_t view;
        if (cluster_wireless_receive_message(frame, sizeof(frame), &frame_size,
                                             &view, pdMS_TO_TICKS(100)) !=
            ESP_OK) {
            continue;
        }
        if (view.type == CLUSTER_MSG_KNOB_COMMAND) {
            handle_knob_command(&view);
        } else if (view.type == CLUSTER_MSG_CLIMATE_STATE) {
            handle_climate_state(&view, frame, frame_size);
        } else if (view.type == CLUSTER_MSG_AIR_RIDE_STATE) {
            cluster_air_ride_state_t state;
            if (cluster_air_ride_state_decode(view.payload, view.payload_size,
                                              &state) != CLUSTER_PROTOCOL_OK) {
                continue;
            }
            air_link_store(&state);
            /* The knob and the left display show live pressures; relay the
             * report unchanged. */
            ESP_ERROR_CHECK_WITHOUT_ABORT(cluster_wireless_send(
                CLUSTER_WIRELESS_ROLE_KNOB | CLUSTER_WIRELESS_ROLE_LEFT, frame,
                frame_size));
        } else if (view.type == CLUSTER_MSG_AIR_RIDE_COMMAND) {
            /* Touch controls on the left display; the right display owns the
             * Air Ride radio. Gated like phone commands. */
            cluster_air_ride_command_t command;
            if (cluster_air_ride_command_decode(view.payload, view.payload_size,
                                                &command) ==
                CLUSTER_PROTOCOL_OK) {
                air_link_web_command(command.code, NULL);
            }
        }
    }
}

void center_link_start(void)
{
    settings_service_set_climate_handlers(climate_online, web_climate_command);
    if (xTaskCreate(receive_task, "cluster_rx", 4096, NULL, 5, NULL) !=
        pdPASS) {
        ESP_LOGE(TAG, "Failed to create cluster receive task");
        abort();
    }
}

void center_link_send_vehicle(const cluster_vehicle_fast_t *vehicle)
{
    if (!cluster_wireless_ready()) return;
    uint8_t payload[CLUSTER_VEHICLE_FAST_PAYLOAD_SIZE];
    ESP_ERROR_CHECK(cluster_vehicle_fast_encode(vehicle, payload,
                                                sizeof(payload)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(cluster_wireless_send_message(
        CLUSTER_WIRELESS_ROLE_SIDES, CLUSTER_MSG_VEHICLE_FAST, payload,
        sizeof(payload)));
}

static esp_err_t send_display_settings(void)
{
    cluster_settings_t settings;
    settings_service_get(&settings);
    const int16_t *thresholds = settings.warning_thresholds;
    cluster_left_settings_t display_settings = {
        .revision = settings.revision,
        .theme = settings.appearance.theme,
        .temperature_celsius = settings.units.temperature_celsius,
        .pressure_kpa = settings.units.pressure_kpa,
        .brightness = settings.appearance.brightness[CLUSTER_SCREEN_LEFT],
        .right_brightness = settings.appearance.brightness[CLUSTER_SCREEN_RIGHT],
        .caution_rgb = settings.appearance.caution_rgb,
        .critical_rgb = settings.appearance.critical_rgb,
        .offline_rgb = settings.appearance.offline_rgb,
        .tach_rgb = settings.appearance.tach_rgb,
        .ect_caution = thresholds[CLUSTER_THRESHOLD_ECT_CAUTION],
        .ect_critical = thresholds[CLUSTER_THRESHOLD_ECT_CRITICAL],
        .iat_caution = thresholds[CLUSTER_THRESHOLD_IAT_CAUTION],
        .iat_critical = thresholds[CLUSTER_THRESHOLD_IAT_CRITICAL],
        .map_caution = thresholds[CLUSTER_THRESHOLD_MAP_CAUTION],
        .map_critical = thresholds[CLUSTER_THRESHOLD_MAP_CRITICAL],
        .battery_caution = thresholds[CLUSTER_THRESHOLD_BATTERY_CAUTION],
        .battery_critical = thresholds[CLUSTER_THRESHOLD_BATTERY_CRITICAL],
    };
    const cluster_screen_layout_settings_t *layout = &settings.screen_layout;
    for (size_t slot = 0; slot < CLUSTER_DATA_SLOT_COUNT; ++slot) {
        display_settings.value_channels[slot] =
            (uint8_t)layout->left_value_channels[slot];
        display_settings.right_value_channels[slot] =
            (uint8_t)layout->right_value_channels[slot];
    }
    for (size_t slot = 0; slot < CLUSTER_GAUGE_SLOT_COUNT; ++slot) {
        display_settings.left_gauge_channels[slot] =
            (uint8_t)layout->left_gauge_channels[slot];
        display_settings.right_gauge_channels[slot] =
            (uint8_t)layout->right_gauge_channels[slot];
    }
    uint8_t payload[CLUSTER_LEFT_SETTINGS_PAYLOAD_SIZE];
    ESP_ERROR_CHECK(cluster_left_settings_encode(&display_settings, payload,
                                                 sizeof(payload)));
    return cluster_wireless_send_message(
        CLUSTER_WIRELESS_ROLE_SIDES | CLUSTER_WIRELESS_ROLE_KNOB,
        CLUSTER_MSG_SETTINGS, payload, sizeof(payload));
}

void center_link_send_status(void)
{
    if (!cluster_wireless_ready()) return;
    ESP_ERROR_CHECK_WITHOUT_ABORT(cluster_wireless_send_message(
        CLUSTER_WIRELESS_ROLE_SIDES | CLUSTER_WIRELESS_ROLE_KNOB,
        CLUSTER_MSG_HEARTBEAT, NULL, 0U));
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_logging_status());
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_display_settings());
    /* Resending keeps the node's link-timeout flag clear and restores the
     * targets if the node rebooted. */
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_climate_target());
}
