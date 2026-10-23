#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "vehicle_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLUSTER_SETTINGS_SCHEMA_VERSION 5U
/* Version 4 records are a prefix of version 5; new fields take defaults. */
#define CLUSTER_SETTINGS_SCHEMA_PREVIOUS_VERSION 4U
#define CLUSTER_SHIFT_STAGE_COUNT 3U
#define CLUSTER_WARNING_THRESHOLD_COUNT 20U
#define CLUSTER_AIR_RIDE_PRESET_COUNT 6U
#define CLUSTER_LEFT_VALUE_SLOT_COUNT 3U
#define CLUSTER_GAUGE_SLOT_COUNT 6U
#define CLUSTER_TREND_SLOT_COUNT 2U

typedef enum {
    CLUSTER_SCREEN_CENTER = 0,
    CLUSTER_SCREEN_LEFT,
    CLUSTER_SCREEN_RIGHT,
    CLUSTER_SCREEN_COUNT,
} cluster_screen_role_t;

typedef enum {
    CLUSTER_CAN_PROTOCOL_AUTO = 0,
    CLUSTER_CAN_PROTOCOL_HONDATA,
    CLUSTER_CAN_PROTOCOL_COUNT,
} cluster_can_protocol_t;

typedef enum {
    CLUSTER_SMOOTHING_OFF = 0,
    CLUSTER_SMOOTHING_LIGHT,
    CLUSTER_SMOOTHING_MEDIUM,
    CLUSTER_SMOOTHING_STRONG,
    CLUSTER_SMOOTHING_COUNT,
} cluster_smoothing_t;

typedef enum {
    CLUSTER_THEME_CLASSIC = 0,
    CLUSTER_THEME_GRAPHITE,
    CLUSTER_THEME_COUNT,
} cluster_theme_t;

typedef enum {
    CLUSTER_LOG_GENERAL = 0,
    CLUSTER_LOG_DYNO,
    CLUSTER_LOG_TUNE,
    CLUSTER_LOG_RACE,
    CLUSTER_LOG_TEST,
    CLUSTER_LOG_NAME_COUNT,
} cluster_log_name_t;

typedef enum {
    CLUSTER_THRESHOLD_ECT_CAUTION = 0,
    CLUSTER_THRESHOLD_ECT_CRITICAL,
    CLUSTER_THRESHOLD_IAT_CAUTION,
    CLUSTER_THRESHOLD_IAT_CRITICAL,
    CLUSTER_THRESHOLD_AFR_RICH_CAUTION,
    CLUSTER_THRESHOLD_AFR_RICH_CRITICAL,
    CLUSTER_THRESHOLD_AFR_LEAN_CAUTION,
    CLUSTER_THRESHOLD_AFR_LEAN_CRITICAL,
    CLUSTER_THRESHOLD_MAP_CAUTION,
    CLUSTER_THRESHOLD_MAP_CRITICAL,
    CLUSTER_THRESHOLD_BATTERY_CAUTION,
    CLUSTER_THRESHOLD_BATTERY_CRITICAL,
    CLUSTER_THRESHOLD_TPS_CAUTION,
    CLUSTER_THRESHOLD_TPS_CRITICAL,
    CLUSTER_THRESHOLD_OIL_CAUTION,
    CLUSTER_THRESHOLD_OIL_CRITICAL,
    CLUSTER_THRESHOLD_DUTY_CAUTION,
    CLUSTER_THRESHOLD_DUTY_CRITICAL,
    CLUSTER_THRESHOLD_KNOCK_CAUTION,
    CLUSTER_THRESHOLD_KNOCK_CRITICAL,
} cluster_warning_threshold_t;

typedef enum {
    CLUSTER_LEFT_SLOT_PRIMARY = 0,
    CLUSTER_LEFT_SLOT_LOWER_LEFT,
    CLUSTER_LEFT_SLOT_LOWER_RIGHT,
} cluster_left_value_slot_t;

typedef struct {
    vehicle_data_channel_t left_value_channels[CLUSTER_LEFT_VALUE_SLOT_COUNT];
    bool right_battery_enabled;
    /* Added in schema 5; must stay at the end of the settings record. */
    vehicle_data_channel_t right_value_channels[CLUSTER_LEFT_VALUE_SLOT_COUNT];
    vehicle_data_channel_t left_gauge_channels[CLUSTER_GAUGE_SLOT_COUNT];
    vehicle_data_channel_t right_gauge_channels[CLUSTER_GAUGE_SLOT_COUNT];
    vehicle_data_channel_t left_trend_channels[CLUSTER_TREND_SLOT_COUNT];
    vehicle_data_channel_t right_trend_channels[CLUSTER_TREND_SLOT_COUNT];
} cluster_screen_layout_settings_t;

typedef struct {
    bool speed_kph;
    bool temperature_celsius;
    bool pressure_kpa;
    bool distance_km;
} cluster_unit_settings_t;

typedef struct {
    uint8_t brightness[CLUSTER_SCREEN_COUNT];
    uint8_t theme;
    uint32_t tach_rgb;
    uint32_t caution_rgb;
    uint32_t critical_rgb;
    uint32_t offline_rgb;
} cluster_appearance_settings_t;

typedef struct {
    uint16_t vtec_rpm;
    uint16_t redline_rpm;
    bool shift_light_enabled;
    uint16_t shift_stage_rpm[CLUSTER_SHIFT_STAGE_COUNT];
    uint32_t shift_stage_rgb[CLUSTER_SHIFT_STAGE_COUNT];
    uint8_t shift_light_brightness;
    bool redline_flash;
    cluster_smoothing_t smoothing;
} cluster_engine_settings_t;

typedef struct {
    uint8_t analog_input;
    uint16_t empty_mv;
    uint16_t full_mv;
} cluster_fuel_settings_t;

typedef struct {
    bool enabled;
    uint16_t compressor_on_psi;
    uint16_t compressor_off_psi;
    uint16_t preset_psi[CLUSTER_AIR_RIDE_PRESET_COUNT];
} cluster_air_ride_settings_t;

typedef struct {
    bool auto_record;
    cluster_log_name_t log_name;
} cluster_logging_settings_t;

typedef struct {
    uint16_t schema_version;
    uint16_t revision;
    cluster_can_protocol_t can_protocol;
    cluster_unit_settings_t units;
    cluster_appearance_settings_t appearance;
    cluster_engine_settings_t engine;
    cluster_fuel_settings_t fuel;
    int16_t warning_thresholds[CLUSTER_WARNING_THRESHOLD_COUNT];
    cluster_air_ride_settings_t air_ride;
    cluster_logging_settings_t logging;
    cluster_screen_layout_settings_t screen_layout;
} cluster_settings_t;

void cluster_settings_set_defaults(cluster_settings_t *settings);
/* Defaults for the schema 5 display-layout fields only. */
void cluster_settings_set_layout_defaults(cluster_settings_t *settings);
bool cluster_settings_validate(cluster_settings_t *settings);

#ifdef __cplusplus
}
#endif
