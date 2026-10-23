#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VEHICLE_DATA_RPM = 0,
    VEHICLE_DATA_SPEED,
    VEHICLE_DATA_BOOST,
    VEHICLE_DATA_COOLANT_TEMP,
    VEHICLE_DATA_INTAKE_TEMP,
    VEHICLE_DATA_AFR,
    VEHICLE_DATA_OIL_PRESSURE,
    VEHICLE_DATA_FUEL_PRESSURE,
    VEHICLE_DATA_BATTERY,
    VEHICLE_DATA_THROTTLE,
    VEHICLE_DATA_FUEL_LEVEL,
    VEHICLE_DATA_IGNITION,
    VEHICLE_DATA_ETHANOL,
    VEHICLE_DATA_KNOCK,
    VEHICLE_DATA_COUNT,
} vehicle_data_channel_t;

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
} vehicle_fast_state_t;

typedef struct {
    uint32_t sample_time_ms;
    uint32_t odometer_tenths_km;
    uint32_t trip_a_tenths_km;
    uint32_t trip_b_tenths_km;
    uint16_t tank_pressure_tenths_psi;
    uint16_t front_left_pressure_tenths_psi;
    uint16_t front_right_pressure_tenths_psi;
    uint16_t rear_left_pressure_tenths_psi;
    uint16_t rear_right_pressure_tenths_psi;
    uint8_t gear;
    bool engine_running;
    bool air_ride_online;
    bool climate_online;
} vehicle_slow_state_t;

static inline uint32_t vehicle_data_channel_mask(vehicle_data_channel_t channel)
{
    return channel < VEHICLE_DATA_COUNT ? (1UL << (uint32_t)channel) : 0UL;
}

static inline bool vehicle_data_channel_valid(uint32_t valid_channels,
                                              uint32_t channel)
{
    return channel < VEHICLE_DATA_COUNT &&
           (valid_channels & (1UL << channel)) != 0U;
}

#ifdef __cplusplus
}
#endif
