#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the speedometer on screen. The needle animates itself whenever
 * screen is the active LVGL screen. Call with the LVGL lock held. */
void side_speedometer_create(lv_obj_t *screen);

/* Sets the target speed. Safe to call from any task without the LVGL lock. */
void side_speedometer_update(uint16_t speed_tenths_kph, bool valid);

/* The remaining calls need the LVGL lock. The graphite theme always uses its
 * own blue accent; the accent colour applies to the classic theme. */
void side_speedometer_set_graphite(bool graphite);
void side_speedometer_set_accent(uint32_t accent_rgb);
void side_speedometer_set_offline(bool offline, uint32_t offline_rgb);

#ifdef __cplusplus
}
#endif
