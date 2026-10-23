#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t odometer_init(void);
void odometer_update(uint16_t speed_tenths_kph, bool speed_valid,
                     uint32_t sample_time_ms);
esp_err_t odometer_periodic_save(void);
esp_err_t odometer_force_save(void);

uint64_t odometer_get_meters(void);
uint64_t odometer_get_trip_a_meters(void);
uint64_t odometer_get_trip_b_meters(void);

esp_err_t odometer_set_meters(uint64_t meters);
esp_err_t odometer_reset_trip_a(void);
esp_err_t odometer_reset_trip_b(void);

#ifdef __cplusplus
}
#endif