#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "lvgl.h"

/* Full-screen Air Ride view drawn over the tach. Needs the LVGL lock. */
void air_overlay_create(lv_obj_t *screen);
void air_overlay_set_theme(size_t theme);
/* Shows the overlay when forced (knob/view selection) or while the right
 * display reports an open adjustment, and refreshes it while shown. */
void air_overlay_update(bool forced_open);
