#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

/* Small LVGL helpers shared by the center dial and its overlays. All need
 * the LVGL lock. */

lv_obj_t *center_ui_label_create(lv_obj_t *parent, const char *text,
                                 const lv_font_t *font, uint32_t color);

/* Skips the redraw when the text is unchanged. */
void center_ui_label_set_text(lv_obj_t *label, const char *text);

/* Full-screen opaque panel used as an overlay above the tach. Starts hidden. */
lv_obj_t *center_ui_panel_create(lv_obj_t *screen, uint32_t color);

/* Describes an embedded PNG for the image decoder. */
void center_ui_png_init(lv_image_dsc_t *descriptor, const uint8_t *start,
                        const uint8_t *end, uint32_t width, uint32_t height);

/* Decodes an opaque PNG once into RGB565 in PSRAM so redraws are plain
 * copies. Leaves the PNG descriptor in place if decoding fails. */
void center_ui_flatten_to_rgb565(lv_image_dsc_t *descriptor);

/* Theme variants are separate image objects; only the active one is shown. */
void center_ui_theme_image_set_active(lv_obj_t *image, bool active);
