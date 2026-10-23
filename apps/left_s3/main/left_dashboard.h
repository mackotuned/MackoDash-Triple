#pragma once

#include <stdbool.h>

#include "cluster_protocol.h"
#include "lvgl.h"

void left_dashboard_create(lv_obj_t *screen, void *context);
void left_dashboard_update(const cluster_vehicle_fast_t *vehicle);
void left_dashboard_apply_settings(const cluster_left_settings_t *settings);
void left_dashboard_apply_display_mode(cluster_display_view_t view);
void left_dashboard_set_offline(bool offline);