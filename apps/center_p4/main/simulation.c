#include "simulation.h"

#define SIMULATION_MIN_RPM 850U
#define SIMULATION_MAX_RPM 9000U
#define SIMULATION_RISE_MS 7000U
#define SIMULATION_HOLD_MS 1000U
#define SIMULATION_FALL_MS 3000U
#define SIMULATION_CYCLE_MS \
    (SIMULATION_RISE_MS + SIMULATION_HOLD_MS + SIMULATION_FALL_MS)
#define SIMULATION_RPM_SPAN (SIMULATION_MAX_RPM - SIMULATION_MIN_RPM)

uint16_t simulation_rpm(uint32_t elapsed_ms)
{
    const uint32_t phase_ms = elapsed_ms % SIMULATION_CYCLE_MS;
    if (phase_ms < SIMULATION_RISE_MS) {
        return (uint16_t)(SIMULATION_MIN_RPM +
            (SIMULATION_RPM_SPAN * phase_ms) / SIMULATION_RISE_MS);
    }
    if (phase_ms < SIMULATION_RISE_MS + SIMULATION_HOLD_MS) {
        return SIMULATION_MAX_RPM;
    }
    return (uint16_t)(SIMULATION_MAX_RPM -
        (SIMULATION_RPM_SPAN *
         (phase_ms - SIMULATION_RISE_MS - SIMULATION_HOLD_MS)) /
            SIMULATION_FALL_MS);
}

void simulation_fill_vehicle(vehicle_fast_state_t *vehicle, uint32_t uptime_ms)
{
    const uint16_t rpm = simulation_rpm(uptime_ms);
    const uint32_t load =
        ((uint32_t)(rpm - SIMULATION_MIN_RPM) * 1000U) / SIMULATION_RPM_SPAN;
    vehicle->sample_time_ms = uptime_ms;
    vehicle->rpm = rpm;
    vehicle->speed_tenths_kph = (uint16_t)(load * 2U);
    vehicle->boost_tenths_kpa = (int16_t)(-600 + (int32_t)load * 2);
    vehicle->coolant_tenths_c = (int16_t)(156 + load * 916U / 1000U);
    vehicle->intake_tenths_c = (int16_t)(200 + load * 400U / 1000U);
    vehicle->afr_thousandths = (uint16_t)(14700 - load * 3U);
    vehicle->oil_pressure_tenths_kpa = (uint16_t)(1500 + load * 3U);
    vehicle->fuel_pressure_tenths_kpa = (uint16_t)(2800 + load / 2U);
    vehicle->battery_mv = (uint16_t)(13800 + load / 2U);
    vehicle->throttle_tenths_percent = (uint16_t)(100 + load * 9U / 10U);
    vehicle->fuel_level_tenths_percent = (uint16_t)(1000U - load);
    vehicle->valid_channels = (1UL << VEHICLE_DATA_COUNT) - 1UL;
    vehicle->warning_channels = 0U;
}
