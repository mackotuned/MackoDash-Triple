#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLUSTER_PROTOCOL_MAGIC 0x4D44U
#define CLUSTER_PROTOCOL_VERSION 1U
#define CLUSTER_PROTOCOL_HEADER_SIZE 14U
#define CLUSTER_PROTOCOL_MAX_PAYLOAD 256U
#define CLUSTER_PROTOCOL_MAX_FRAME_SIZE \
    (CLUSTER_PROTOCOL_HEADER_SIZE + CLUSTER_PROTOCOL_MAX_PAYLOAD)
#define CLUSTER_AIR_RIDE_PAYLOAD_VERSION 1U
#define CLUSTER_AIR_RIDE_STATE_PAYLOAD_SIZE 14U
#define CLUSTER_AIR_RIDE_COMMAND_PAYLOAD_SIZE 6U
#define CLUSTER_VEHICLE_FAST_PAYLOAD_VERSION 3U
#define CLUSTER_VEHICLE_FAST_PAYLOAD_SIZE 42U
#define CLUSTER_LEFT_SETTINGS_PAYLOAD_VERSION 4U
#define CLUSTER_LEFT_SETTINGS_PAYLOAD_SIZE 61U
#define CLUSTER_DATA_SLOT_COUNT 3U
#define CLUSTER_GAUGE_SLOT_COUNT 6U
#define CLUSTER_TREND_SLOT_COUNT 2U
#define CLUSTER_KNOB_COMMAND_PAYLOAD_VERSION 1U
#define CLUSTER_KNOB_COMMAND_PAYLOAD_SIZE 8U
#define CLUSTER_DISPLAY_MODE_PAYLOAD_VERSION 1U
#define CLUSTER_DISPLAY_MODE_PAYLOAD_SIZE 4U
#define CLUSTER_LOGGING_STATUS_PAYLOAD_VERSION 1U
#define CLUSTER_LOGGING_STATUS_PAYLOAD_SIZE 12U
#define CLUSTER_CLIMATE_PAYLOAD_VERSION 1U
#define CLUSTER_CLIMATE_COMMAND_PAYLOAD_SIZE 4U
#define CLUSTER_CLIMATE_STATE_PAYLOAD_SIZE 10U

/* Climate dial positions, matching the Civic control board detents. */
#define CLUSTER_CLIMATE_FAN_MAX 10U  /* 0 off, 1-10 */
#define CLUSTER_CLIMATE_MODE_MAX 4U  /* face .. defrost */
#define CLUSTER_CLIMATE_TEMP_MAX 14U /* 0 full cold .. 14 full hot */

typedef enum {
    CLUSTER_MSG_HEARTBEAT = 0x01,
    CLUSTER_MSG_VEHICLE_FAST = 0x02,
    CLUSTER_MSG_VEHICLE_SLOW = 0x03,
    CLUSTER_MSG_WARNINGS = 0x04,
    CLUSTER_MSG_SETTINGS = 0x05,
    CLUSTER_MSG_AIR_RIDE_STATE = 0x06,
    CLUSTER_MSG_AIR_RIDE_COMMAND = 0x07,
    CLUSTER_MSG_KNOB_COMMAND = 0x08,
    CLUSTER_MSG_DISPLAY_MODE = 0x09,
    CLUSTER_MSG_LOGGING_STATUS = 0x0A,
    CLUSTER_MSG_CLIMATE_COMMAND = 0x0B,
    CLUSTER_MSG_CLIMATE_STATE = 0x0C,
} cluster_message_type_t;

typedef enum {
    CLUSTER_KNOB_SCREEN_SET = 1,
    CLUSTER_KNOB_RECORD_SET = 2,
    CLUSTER_KNOB_AIR_SET = 3,
    CLUSTER_KNOB_HVAC_FAN_SET = 4,
    CLUSTER_KNOB_HVAC_BLEND_SET = 5,
    CLUSTER_KNOB_HVAC_MODE_SET = 6,
    CLUSTER_KNOB_LOGS_OPEN = 7,
    CLUSTER_KNOB_BRIGHTNESS_SET = 8,
    CLUSTER_KNOB_AIR_VIEW_SET = 9,
    CLUSTER_KNOB_COMMAND_FIRST = CLUSTER_KNOB_SCREEN_SET,
    CLUSTER_KNOB_COMMAND_LAST = CLUSTER_KNOB_AIR_VIEW_SET,
} cluster_knob_command_code_t;

typedef enum {
    CLUSTER_DISPLAY_LEFT = 0,
    CLUSTER_DISPLAY_CENTER = 1,
    CLUSTER_DISPLAY_RIGHT = 2,
    CLUSTER_DISPLAY_COUNT,
} cluster_display_target_t;

typedef enum {
    CLUSTER_VIEW_TACHOMETER = 0,
    CLUSTER_VIEW_SPEEDOMETER = 1,
    CLUSTER_VIEW_DATA = 2,
    CLUSTER_VIEW_AIR_RIDE = 3,
    CLUSTER_VIEW_LOGS = 4,
    CLUSTER_VIEW_CLIMATE = 5,
    CLUSTER_VIEW_GAUGES = 6,
    CLUSTER_VIEW_TRENDS = 7,
    CLUSTER_VIEW_PEAKS = 8,
    CLUSTER_VIEW_TIMER = 9,
    CLUSTER_VIEW_COUNT,
} cluster_display_view_t;

typedef struct {
    cluster_display_target_t target;
    cluster_display_view_t view;
} cluster_display_mode_t;

typedef struct {
    bool available;
    bool recording;
    uint16_t session_index;
    uint32_t record_count;
    uint32_t dropped_count;
} cluster_logging_status_t;

typedef enum {
    CLUSTER_AIR_TARGET_ALL = 0,
    CLUSTER_AIR_TARGET_FRONT,
    CLUSTER_AIR_TARGET_REAR,
    CLUSTER_AIR_TARGET_FRONT_LEFT,
    CLUSTER_AIR_TARGET_FRONT_RIGHT,
    CLUSTER_AIR_TARGET_REAR_LEFT,
    CLUSTER_AIR_TARGET_REAR_RIGHT,
    CLUSTER_AIR_TARGET_COUNT,
} cluster_air_target_t;

enum {
    CLUSTER_KNOB_COMMAND_LIVE = 1U << 0,
    CLUSTER_KNOB_COMMAND_CANCEL = 1U << 1,
    CLUSTER_KNOB_COMMAND_CONFIRM = 1U << 2,
    CLUSTER_KNOB_COMMAND_FLAGS = CLUSTER_KNOB_COMMAND_LIVE |
                                 CLUSTER_KNOB_COMMAND_CANCEL |
                                 CLUSTER_KNOB_COMMAND_CONFIRM,
};

typedef struct {
    cluster_knob_command_code_t code;
    uint8_t target;
    uint8_t flags;
    int32_t value;
} cluster_knob_command_t;

typedef enum {
    CLUSTER_AIR_RIDE_COMPRESSOR_OFF = 0,
    CLUSTER_AIR_RIDE_COMPRESSOR_ON = 1,
    CLUSTER_AIR_RIDE_FRONT_LEFT_UP = 2,
    CLUSTER_AIR_RIDE_FRONT_LEFT_DOWN = 3,
    CLUSTER_AIR_RIDE_FRONT_RIGHT_UP = 4,
    CLUSTER_AIR_RIDE_FRONT_RIGHT_DOWN = 5,
    CLUSTER_AIR_RIDE_REAR_LEFT_UP = 6,
    CLUSTER_AIR_RIDE_REAR_LEFT_DOWN = 7,
    CLUSTER_AIR_RIDE_REAR_RIGHT_UP = 8,
    CLUSTER_AIR_RIDE_REAR_RIGHT_DOWN = 9,
    CLUSTER_AIR_RIDE_ALL_UP = 10,
    CLUSTER_AIR_RIDE_ALL_DOWN = 11,
    CLUSTER_AIR_RIDE_RELEASE = 12,
    CLUSTER_AIR_RIDE_FRONT_UP = 13,
    CLUSTER_AIR_RIDE_FRONT_DOWN = 14,
    CLUSTER_AIR_RIDE_REAR_UP = 15,
    CLUSTER_AIR_RIDE_REAR_DOWN = 16,
} cluster_air_ride_command_code_t;

typedef struct {
    bool connected;
    bool pressure_valid;
    bool compressor_valid;
    bool compressor_on;
    bool adjustment_open;
    uint8_t adjustment_target;
    uint16_t tank_psi;
    uint16_t front_left_psi;
    uint16_t front_right_psi;
    uint16_t rear_left_psi;
    uint16_t rear_right_psi;
} cluster_air_ride_state_t;

typedef struct {
    cluster_air_ride_command_code_t code;
    uint16_t compressor_on_psi;
    uint16_t compressor_off_psi;
} cluster_air_ride_command_t;

typedef struct {
    uint32_t sample_time_ms;
    uint16_t rpm;
    uint16_t speed_tenths_kph;
    int16_t boost_tenths_kpa;
    int16_t coolant_tenths_c;
    int16_t intake_tenths_c;
    uint16_t afr_thousandths;
    uint16_t oil_pressure_tenths_kpa;
    uint16_t fuel_pressure_tenths_kpa;
    uint16_t battery_mv;
    uint16_t throttle_tenths_percent;
    uint16_t fuel_level_tenths_percent;
    int16_t ignition_deg;
    uint16_t ethanol_tenths_percent;
    uint16_t knock_count;
    uint32_t valid_channels;
    uint32_t warning_channels;
} cluster_vehicle_fast_t;

typedef struct {
    uint16_t revision;
    uint8_t theme;
    bool temperature_celsius;
    bool pressure_kpa;
    uint8_t brightness;
    uint8_t right_brightness;
    uint32_t caution_rgb;
    uint32_t critical_rgb;
    uint32_t offline_rgb;
    uint32_t tach_rgb;
    int16_t ect_caution;
    int16_t ect_critical;
    int16_t iat_caution;
    int16_t iat_critical;
    int16_t map_caution;
    int16_t map_critical;
    int16_t battery_caution;
    int16_t battery_critical;
    /* Data page slots and gauge grid per side display. */
    uint8_t value_channels[CLUSTER_DATA_SLOT_COUNT];
    uint8_t right_value_channels[CLUSTER_DATA_SLOT_COUNT];
    uint8_t left_gauge_channels[CLUSTER_GAUGE_SLOT_COUNT];
    uint8_t right_gauge_channels[CLUSTER_GAUGE_SLOT_COUNT];
    uint8_t left_trend_channels[CLUSTER_TREND_SLOT_COUNT];
    uint8_t right_trend_channels[CLUSTER_TREND_SLOT_COUNT];
} cluster_left_settings_t;

/* Target dial positions, P4 to the climate gateway display. */
typedef struct {
    uint8_t fan;
    uint8_t mode;
    uint8_t temp;
} cluster_climate_command_t;

/* Climate node report, gateway display to the P4 (relayed to the knob). */
typedef struct {
    bool connected;
    uint8_t fan;
    uint8_t mode;
    uint8_t temp;
    uint8_t target_fan;
    uint8_t target_mode;
    uint8_t target_temp;
    uint8_t moving_mask;
    uint8_t fault_flags;
} cluster_climate_state_t;

typedef enum {
    CLUSTER_PROTOCOL_OK = 0,
    CLUSTER_PROTOCOL_INVALID_ARGUMENT,
    CLUSTER_PROTOCOL_BUFFER_TOO_SMALL,
    CLUSTER_PROTOCOL_BAD_MAGIC,
    CLUSTER_PROTOCOL_BAD_VERSION,
    CLUSTER_PROTOCOL_BAD_LENGTH,
    CLUSTER_PROTOCOL_BAD_CRC,
} cluster_protocol_result_t;

typedef struct {
    cluster_message_type_t type;
    uint16_t sequence;
    uint32_t uptime_ms;
    const uint8_t *payload;
    uint16_t payload_size;
} cluster_frame_view_t;

uint16_t cluster_crc16_ccitt(const uint8_t *data, size_t size);

/* Raise or lower command for an air ride target (cluster_air_target_t). */
cluster_air_ride_command_code_t cluster_air_target_command(uint8_t target,
                                                           bool raise);

/* Round-trips a frame through encode and decode; logged at boot. */
bool cluster_protocol_self_test(void);

cluster_protocol_result_t cluster_frame_encode(
    cluster_message_type_t type,
    uint16_t sequence,
    uint32_t uptime_ms,
    const void *payload,
    uint16_t payload_size,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_size);

cluster_protocol_result_t cluster_frame_decode(
    const uint8_t *frame,
    size_t frame_size,
    cluster_frame_view_t *view);

cluster_protocol_result_t cluster_air_ride_state_encode(
    const cluster_air_ride_state_t *state,
    uint8_t *payload,
    size_t payload_capacity);
cluster_protocol_result_t cluster_air_ride_state_decode(
    const uint8_t *payload,
    size_t payload_size,
    cluster_air_ride_state_t *state);
cluster_protocol_result_t cluster_air_ride_command_encode(
    const cluster_air_ride_command_t *command,
    uint8_t *payload,
    size_t payload_capacity);
cluster_protocol_result_t cluster_air_ride_command_decode(
    const uint8_t *payload,
    size_t payload_size,
    cluster_air_ride_command_t *command);
cluster_protocol_result_t cluster_vehicle_fast_encode(
    const cluster_vehicle_fast_t *vehicle,
    uint8_t *payload,
    size_t payload_capacity);
cluster_protocol_result_t cluster_vehicle_fast_decode(
    const uint8_t *payload,
    size_t payload_size,
    cluster_vehicle_fast_t *vehicle);
cluster_protocol_result_t cluster_left_settings_encode(
    const cluster_left_settings_t *settings,
    uint8_t *payload,
    size_t payload_capacity);
cluster_protocol_result_t cluster_left_settings_decode(
    const uint8_t *payload,
    size_t payload_size,
    cluster_left_settings_t *settings);
cluster_protocol_result_t cluster_knob_command_encode(
    const cluster_knob_command_t *command,
    uint8_t *payload,
    size_t payload_capacity);
cluster_protocol_result_t cluster_knob_command_decode(
    const uint8_t *payload,
    size_t payload_size,
    cluster_knob_command_t *command);
cluster_protocol_result_t cluster_display_mode_encode(
    const cluster_display_mode_t *mode,
    uint8_t *payload,
    size_t payload_capacity);
cluster_protocol_result_t cluster_display_mode_decode(
    const uint8_t *payload,
    size_t payload_size,
    cluster_display_mode_t *mode);
cluster_protocol_result_t cluster_logging_status_encode(
    const cluster_logging_status_t *status,
    uint8_t *payload,
    size_t payload_capacity);
cluster_protocol_result_t cluster_logging_status_decode(
    const uint8_t *payload,
    size_t payload_size,
    cluster_logging_status_t *status);
cluster_protocol_result_t cluster_climate_command_encode(
    const cluster_climate_command_t *command,
    uint8_t *payload,
    size_t payload_capacity);
cluster_protocol_result_t cluster_climate_command_decode(
    const uint8_t *payload,
    size_t payload_size,
    cluster_climate_command_t *command);
cluster_protocol_result_t cluster_climate_state_encode(
    const cluster_climate_state_t *state,
    uint8_t *payload,
    size_t payload_capacity);
cluster_protocol_result_t cluster_climate_state_decode(
    const uint8_t *payload,
    size_t payload_size,
    cluster_climate_state_t *state);

#ifdef __cplusplus
}
#endif
