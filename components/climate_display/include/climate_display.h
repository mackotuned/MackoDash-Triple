#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *climate_display_create(void);
void climate_display_set_graphite(bool graphite);
void climate_display_set_offline(bool offline);

#ifdef __cplusplus
}
#endif
