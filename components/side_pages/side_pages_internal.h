#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "side_pages.h"

/* Page modules used by side_pages.c. All calls need the LVGL lock. */

bool side_pages_center_online(void);
lv_obj_t *side_pages_label_create(lv_obj_t *parent, const char *text,
                                  const lv_font_t *font, uint32_t color);
void side_pages_label_set_text(lv_obj_t *label, const char *text);

/* Value text, label, unit and warning colour for a vehicle channel. */
int32_t side_tenths_c_to_tenths_f(int16_t tenths_c);
const char *side_channel_name(uint8_t channel);
const char *side_channel_unit(const cluster_vehicle_fast_t *vehicle,
                              uint8_t channel);
void side_channel_format(char *output, size_t output_size,
                         const cluster_vehicle_fast_t *vehicle,
                         uint8_t channel);
uint32_t side_channel_color(const cluster_vehicle_fast_t *vehicle,
                            uint8_t channel,
                            const cluster_left_settings_t *settings);

/* Data page: speed, a large primary value, two smaller values and the fuel
 * and water arcs. value_channels in the settings are this display's slots. */
void data_page_create(lv_obj_t *screen);
void data_page_apply_settings(const cluster_left_settings_t *settings);
void data_page_update(const cluster_vehicle_fast_t *vehicle);
void data_page_set_offline(bool offline);

/* Gauges page: six configurable values in a 2x3 grid. */
void gauges_page_create(lv_obj_t *screen);
void gauges_page_apply_settings(const cluster_left_settings_t *settings,
                                const uint8_t *channels);
void gauges_page_update(const cluster_vehicle_fast_t *vehicle);
void gauges_page_set_offline(bool offline);

void air_page_create(lv_obj_t *screen, const side_air_backend_t *backend);
void air_page_set_graphite(bool graphite);
/* Releases any held command and closes the adjustment overlay. */
void air_page_leave(void);
