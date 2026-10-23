#pragma once

#include <stdint.h>

#include "vehicle_data.h"

/* Bench simulation (enabled from the phone UI): RPM ramps from idle to the
 * limiter, holds, then falls back, on a repeating 11 second cycle. */
uint16_t simulation_rpm(uint32_t elapsed_ms);

/* Fills every channel with plausible values that track the simulated RPM. */
void simulation_fill_vehicle(vehicle_fast_state_t *vehicle,
                             uint32_t uptime_ms);
