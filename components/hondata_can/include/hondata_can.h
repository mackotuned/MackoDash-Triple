#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "vehicle_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HONDATA_CAN_ID_FIRST 0x660U
#define HONDATA_CAN_ID_LAST 0x669U
#define HONDATA_CAN_FRAME_COUNT 10U

typedef enum {
    HONDATA_DECODE_OK = 0,
    HONDATA_DECODE_NOT_HONDATA,
    HONDATA_DECODE_INVALID_ARGUMENT,
    HONDATA_DECODE_INVALID_LENGTH,
} hondata_decode_result_t;

typedef struct {
    uint32_t received_frame_mask;
    uint32_t last_frame_time_ms[HONDATA_CAN_FRAME_COUNT];
    uint16_t rpm;
    uint16_t speed_tenths_kph;
    uint8_t gear;
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
} hondata_state_t;

typedef enum {
    HONDATA_CAN_OFFLINE = 0,
    HONDATA_CAN_SCANNING,
    HONDATA_CAN_RUNNING,
    HONDATA_CAN_BUS_OFF,
    HONDATA_CAN_RECOVERING,
} hondata_can_controller_state_t;

typedef struct {
    hondata_can_controller_state_t controller_state;
    bool live_data;
    uint32_t bitrate;
    uint32_t last_frame_age_ms;
    uint32_t received_frame_count;
    uint32_t invalid_length_count;
    uint32_t receive_missed_count;
    uint32_t bus_error_count;
    uint32_t rx_error_counter;
} hondata_can_diagnostics_t;

void hondata_state_reset(hondata_state_t *state);
hondata_decode_result_t hondata_decode_frame(
    hondata_state_t *state,
    uint32_t identifier,
    const uint8_t *data,
    size_t data_length,
    uint32_t sample_time_ms);
void hondata_apply_vehicle_fast(const hondata_state_t *hondata,
                                vehicle_fast_state_t *vehicle);
void hondata_apply_fuel_level(const hondata_state_t *hondata,
                              uint8_t analog_input,
                              uint16_t empty_mv,
                              uint16_t full_mv,
                              vehicle_fast_state_t *vehicle);
esp_err_t hondata_can_start(void);
void hondata_can_get_state(hondata_state_t *state);
void hondata_can_get_vehicle_fast(vehicle_fast_state_t *vehicle);
void hondata_can_get_diagnostics(hondata_can_diagnostics_t *diagnostics);

#ifdef __cplusplus
}
#endif