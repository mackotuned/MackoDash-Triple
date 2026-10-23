#pragma once

#include <stdbool.h>

#include "hondata_can.h"
#include "lvgl.h"

/* Full-screen Hondata log view drawn over the tach. Needs the LVGL lock. */
void log_overlay_create(lv_obj_t *screen);
/* Shows the newest logged sample, or live data when nothing is logged. */
void log_overlay_update(bool open, const hondata_state_t *live_state);
