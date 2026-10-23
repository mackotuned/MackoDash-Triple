#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*side_display_builder_t)(lv_obj_t *screen, void *context);

/* Brings up the Waveshare 2.8C panel and LVGL, then calls builder with the
 * LVGL lock held to create the UI on the active screen. */
esp_err_t side_display_start(side_display_builder_t builder, void *context,
                             bool enable_touch);
esp_err_t side_display_lock(int32_t timeout_ms);
void side_display_unlock(void);
esp_err_t side_display_set_brightness(uint8_t percent);

#ifdef __cplusplus
}
#endif