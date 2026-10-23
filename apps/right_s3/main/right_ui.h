#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cluster_protocol.h"
#include "lvgl.h"

/* Builds the air ride, speedometer and climate screens; runs with the LVGL
 * lock held as the side_display builder. */
void right_ui_create(lv_obj_t *screen, void *context);

/* The calls below take the LVGL lock themselves. */
void right_ui_set_center_online(bool online);
void right_ui_apply_settings(const cluster_left_settings_t *settings);
void right_ui_show_view(cluster_display_view_t view);

/* Lock-free state shared with the wireless loop. */
bool right_ui_controls_armed(void);
void right_ui_get_adjustment(bool *open, uint8_t *target);
/* A remote (knob/phone) command opens the adjustment overlay on target while
 * it is moving the car, and closes it on release. */
void right_ui_remote_adjustment(bool active, uint8_t target);
