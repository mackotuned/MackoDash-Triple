#include "settings_schema.h"

#include <string.h>

#define RGB24(value) ((uint32_t)(value) & 0x00FFFFFFU)

static const int16_t threshold_defaults[CLUSTER_WARNING_THRESHOLD_COUNT] = {
    1950, 2050, 1000, 1100, 108, 105, 112, 115, 120, 160,
    130, 120, 700, 800, 200, 100, 650, 750, 20, 40,
};

static const int16_t threshold_minimums[CLUSTER_WARNING_THRESHOLD_COUNT] = {
    1600, 1600, 600, 600, 80, 80, 100, 100, 0, 0,
    80, 80, 500, 500, 0, 0, 400, 400, 0, 10,
};

static const int16_t threshold_maximums[CLUSTER_WARNING_THRESHOLD_COUNT] = {
    2600, 2600, 2000, 2000, 140, 140, 200, 200, 400, 400,
    150, 150, 1000, 1000, 500, 500, 1000, 1000, 80, 100,
};

static const int16_t threshold_steps[CLUSTER_WARNING_THRESHOLD_COUNT] = {
    10, 10, 10, 10, 1, 1, 1, 1, 5, 5,
    1, 1, 10, 10, 10, 10, 10, 10, 1, 1,
};

static const vehicle_data_channel_t left_value_defaults[CLUSTER_LEFT_VALUE_SLOT_COUNT] = {
    VEHICLE_DATA_BOOST, VEHICLE_DATA_INTAKE_TEMP, VEHICLE_DATA_BATTERY,
};
static const vehicle_data_channel_t right_value_defaults[CLUSTER_LEFT_VALUE_SLOT_COUNT] = {
    VEHICLE_DATA_BOOST, VEHICLE_DATA_COOLANT_TEMP, VEHICLE_DATA_AFR,
};
static const vehicle_data_channel_t left_gauge_defaults[CLUSTER_GAUGE_SLOT_COUNT] = {
    VEHICLE_DATA_COOLANT_TEMP, VEHICLE_DATA_INTAKE_TEMP, VEHICLE_DATA_AFR,
    VEHICLE_DATA_OIL_PRESSURE, VEHICLE_DATA_BATTERY, VEHICLE_DATA_ETHANOL,
};
static const vehicle_data_channel_t right_gauge_defaults[CLUSTER_GAUGE_SLOT_COUNT] = {
    VEHICLE_DATA_BOOST, VEHICLE_DATA_THROTTLE, VEHICLE_DATA_IGNITION,
    VEHICLE_DATA_KNOCK, VEHICLE_DATA_FUEL_PRESSURE, VEHICLE_DATA_FUEL_LEVEL,
};

/* Replaces out-of-range channel selections with their defaults. */
static bool validate_channels(vehicle_data_channel_t *channels,
                              const vehicle_data_channel_t *defaults,
                              size_t count)
{
    bool unchanged = true;
    for (size_t index = 0; index < count; ++index) {
        if (channels[index] >= VEHICLE_DATA_COUNT) {
            channels[index] = defaults[index];
            unchanged = false;
        }
    }
    return unchanged;
}

void cluster_settings_set_layout_defaults(cluster_settings_t *settings)
{
    cluster_screen_layout_settings_t *layout = &settings->screen_layout;
    memcpy(layout->right_value_channels, right_value_defaults,
           sizeof(right_value_defaults));
    memcpy(layout->left_gauge_channels, left_gauge_defaults,
           sizeof(left_gauge_defaults));
    memcpy(layout->right_gauge_channels, right_gauge_defaults,
           sizeof(right_gauge_defaults));
}

static uint16_t clamp_u16(uint16_t value, uint16_t minimum, uint16_t maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

void cluster_settings_set_defaults(cluster_settings_t *settings)
{
    if (!settings) return;

    memset(settings, 0, sizeof(*settings));
    settings->schema_version = CLUSTER_SETTINGS_SCHEMA_VERSION;
    settings->revision = 1U;
    settings->can_protocol = CLUSTER_CAN_PROTOCOL_HONDATA;
    settings->appearance.brightness[CLUSTER_SCREEN_CENTER] = 95U;
    settings->appearance.brightness[CLUSTER_SCREEN_LEFT] = 95U;
    settings->appearance.brightness[CLUSTER_SCREEN_RIGHT] = 95U;
    settings->appearance.theme = CLUSTER_THEME_CLASSIC;
    settings->appearance.tach_rgb = 0xEC5C28U;
    settings->appearance.caution_rgb = 0xFFB020U;
    settings->appearance.critical_rgb = 0xE4002BU;
    settings->appearance.offline_rgb = 0x84868DU;
    settings->engine.vtec_rpm = 5600U;
    settings->engine.redline_rpm = 8400U;
    settings->engine.shift_light_enabled = true;
    settings->engine.shift_stage_rpm[0] = 6500U;
    settings->engine.shift_stage_rpm[1] = 7400U;
    settings->engine.shift_stage_rpm[2] = 8200U;
    settings->engine.shift_stage_rgb[0] = 0x39FF8CU;
    settings->engine.shift_stage_rgb[1] = 0xFFE066U;
    settings->engine.shift_stage_rgb[2] = 0xE4002BU;
    settings->engine.shift_light_brightness = 100U;
    settings->engine.redline_flash = true;
    settings->engine.smoothing = CLUSTER_SMOOTHING_OFF;
    settings->fuel.analog_input = 0U;
    settings->fuel.empty_mv = 3290U;
    settings->fuel.full_mv = 290U;
    memcpy(settings->warning_thresholds, threshold_defaults, sizeof(threshold_defaults));
    settings->air_ride.compressor_on_psi = 120U;
    settings->air_ride.compressor_off_psi = 150U;
    settings->air_ride.preset_psi[0] = 85U;
    settings->air_ride.preset_psi[1] = 95U;
    settings->air_ride.preset_psi[2] = 40U;
    settings->air_ride.preset_psi[3] = 1U;
    settings->air_ride.preset_psi[4] = 70U;
    settings->air_ride.preset_psi[5] = 110U;
    settings->logging.log_name = CLUSTER_LOG_GENERAL;
    memcpy(settings->screen_layout.left_value_channels, left_value_defaults,
           sizeof(left_value_defaults));
    settings->screen_layout.right_battery_enabled = true;
    cluster_settings_set_layout_defaults(settings);
}

bool cluster_settings_validate(cluster_settings_t *settings)
{
    if (!settings || settings->schema_version != CLUSTER_SETTINGS_SCHEMA_VERSION) return false;

    bool unchanged = true;
    if (settings->can_protocol >= CLUSTER_CAN_PROTOCOL_COUNT) {
        settings->can_protocol = CLUSTER_CAN_PROTOCOL_HONDATA;
        unchanged = false;
    }
    for (size_t index = 0; index < CLUSTER_SCREEN_COUNT; ++index) {
        uint8_t brightness = settings->appearance.brightness[index];
        settings->appearance.brightness[index] = (uint8_t)clamp_u16(brightness, 20U, 100U);
        unchanged &= brightness == settings->appearance.brightness[index];
    }
    if (settings->appearance.theme >= CLUSTER_THEME_COUNT) {
        settings->appearance.theme = CLUSTER_THEME_CLASSIC;
        unchanged = false;
    }
    settings->appearance.tach_rgb = RGB24(settings->appearance.tach_rgb);
    settings->appearance.caution_rgb = RGB24(settings->appearance.caution_rgb);
    settings->appearance.critical_rgb = RGB24(settings->appearance.critical_rgb);
    settings->appearance.offline_rgb = RGB24(settings->appearance.offline_rgb);

    uint16_t vtec = settings->engine.vtec_rpm;
    uint16_t redline = settings->engine.redline_rpm;
    settings->engine.redline_rpm = clamp_u16(redline, 6000U, 9000U);
    settings->engine.vtec_rpm = clamp_u16(vtec, 3000U, 7500U);
    if (settings->engine.vtec_rpm + 600U > settings->engine.redline_rpm) {
        settings->engine.vtec_rpm = settings->engine.redline_rpm - 600U;
    }
    unchanged &= vtec == settings->engine.vtec_rpm && redline == settings->engine.redline_rpm;
    for (size_t index = 0; index < CLUSTER_SHIFT_STAGE_COUNT; ++index) {
        uint16_t rpm = settings->engine.shift_stage_rpm[index];
        uint16_t maximum = 9000U -
            (uint16_t)((CLUSTER_SHIFT_STAGE_COUNT - index - 1U) * 100U);
        settings->engine.shift_stage_rpm[index] = clamp_u16(rpm, 3000U, maximum);
        settings->engine.shift_stage_rgb[index] = RGB24(settings->engine.shift_stage_rgb[index]);
        unchanged &= rpm == settings->engine.shift_stage_rpm[index];
    }
    for (size_t index = 1; index < CLUSTER_SHIFT_STAGE_COUNT; ++index) {
        if (settings->engine.shift_stage_rpm[index] < settings->engine.shift_stage_rpm[index - 1] + 100U) {
            settings->engine.shift_stage_rpm[index] = settings->engine.shift_stage_rpm[index - 1] + 100U;
            unchanged = false;
        }
    }
    uint8_t shift_brightness = settings->engine.shift_light_brightness;
    settings->engine.shift_light_brightness = (uint8_t)clamp_u16(shift_brightness, 20U, 100U);
    unchanged &= shift_brightness == settings->engine.shift_light_brightness;
    if (settings->engine.smoothing >= CLUSTER_SMOOTHING_COUNT) {
        settings->engine.smoothing = CLUSTER_SMOOTHING_OFF;
        unchanged = false;
    }

    if (settings->fuel.analog_input >= 8U) {
        settings->fuel.analog_input = 0U;
        unchanged = false;
    }
    settings->fuel.empty_mv = clamp_u16(settings->fuel.empty_mv, 0U, 5000U);
    settings->fuel.full_mv = clamp_u16(settings->fuel.full_mv, 0U, 5000U);

    for (size_t index = 0; index < CLUSTER_WARNING_THRESHOLD_COUNT; ++index) {
        int16_t value = settings->warning_thresholds[index];
        if (value < threshold_minimums[index]) value = threshold_minimums[index];
        if (value > threshold_maximums[index]) value = threshold_maximums[index];
        unchanged &= value == settings->warning_thresholds[index];
        settings->warning_thresholds[index] = value;
    }
    for (size_t index = 0; index < CLUSTER_WARNING_THRESHOLD_COUNT; index += 2U) {
        bool lower_is_critical = index == CLUSTER_THRESHOLD_AFR_RICH_CAUTION ||
                                 index == CLUSTER_THRESHOLD_BATTERY_CAUTION ||
                                 index == CLUSTER_THRESHOLD_OIL_CAUTION;
        if (lower_is_critical) {
            if (settings->warning_thresholds[index] <= settings->warning_thresholds[index + 1U]) {
                settings->warning_thresholds[index] = settings->warning_thresholds[index + 1U] + threshold_steps[index];
                unchanged = false;
            }
        } else if (settings->warning_thresholds[index] >= settings->warning_thresholds[index + 1U]) {
            settings->warning_thresholds[index] = settings->warning_thresholds[index + 1U] - threshold_steps[index];
            unchanged = false;
        }
    }

    uint16_t compressor_on = settings->air_ride.compressor_on_psi;
    uint16_t compressor_off = settings->air_ride.compressor_off_psi;
    settings->air_ride.compressor_on_psi = clamp_u16(compressor_on, 80U, 195U);
    settings->air_ride.compressor_off_psi = clamp_u16(compressor_off, 85U, 200U);
    if (settings->air_ride.compressor_on_psi + 5U > settings->air_ride.compressor_off_psi) {
        settings->air_ride.compressor_off_psi = settings->air_ride.compressor_on_psi + 5U;
    }
    unchanged &= compressor_on == settings->air_ride.compressor_on_psi &&
                 compressor_off == settings->air_ride.compressor_off_psi;
    for (size_t index = 0; index < CLUSTER_AIR_RIDE_PRESET_COUNT; ++index) {
        uint16_t preset = settings->air_ride.preset_psi[index];
        settings->air_ride.preset_psi[index] = clamp_u16(preset, 0U, 200U);
        unchanged &= preset == settings->air_ride.preset_psi[index];
    }
    if (settings->logging.log_name >= CLUSTER_LOG_NAME_COUNT) {
        settings->logging.log_name = CLUSTER_LOG_GENERAL;
        unchanged = false;
    }
    cluster_screen_layout_settings_t *layout = &settings->screen_layout;
    unchanged &= validate_channels(layout->left_value_channels,
                                   left_value_defaults,
                                   CLUSTER_LEFT_VALUE_SLOT_COUNT);
    unchanged &= validate_channels(layout->right_value_channels,
                                   right_value_defaults,
                                   CLUSTER_LEFT_VALUE_SLOT_COUNT);
    unchanged &= validate_channels(layout->left_gauge_channels,
                                   left_gauge_defaults,
                                   CLUSTER_GAUGE_SLOT_COUNT);
    unchanged &= validate_channels(layout->right_gauge_channels,
                                   right_gauge_defaults,
                                   CLUSTER_GAUGE_SLOT_COUNT);
    return unchanged;
}