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

/* ---- Channels ------------------------------------------------------------ */

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
/* Numeric value in tenths of the displayed unit (boost as signed PSI);
 * false when the channel has no live data. */
bool side_channel_value_tenths(const cluster_vehicle_fast_t *vehicle,
                               uint8_t channel, int32_t *value);
/* Plot range for a channel, in the same tenths. */
void side_channel_range_tenths(uint8_t channel, int32_t *minimum,
                               int32_t *maximum);

/* ---- Look ---------------------------------------------------------------- */

typedef struct {
    uint32_t background;
    uint32_t panel;
    uint32_t border;
    uint32_t text;
    uint32_t dim;
    uint32_t accent;  /* classic red, graphite blue */
    uint32_t accent2; /* second trace / secondary highlight */
    uint32_t grid;
} side_palette_t;

const side_palette_t *side_palette(bool graphite);
/* Thin ring around the panel edge shown while the P4 is offline. */
lv_obj_t *side_offline_ring_create(lv_obj_t *screen);
/* Rounded tile; colour it with side_panel_style(). */
lv_obj_t *side_panel_create(lv_obj_t *parent, int32_t x, int32_t y,
                            int32_t width, int32_t height);
void side_panel_style(lv_obj_t *panel, const side_palette_t *palette);

/* ---- Data-driven pages ----------------------------------------------------- */

/* Every page built from live vehicle data implements this. apply_settings
 * gets the display it runs on so it can pick that display's slots. */
typedef struct {
    void (*create)(lv_obj_t *screen);
    void (*apply_settings)(const cluster_left_settings_t *settings,
                           cluster_display_target_t display);
    void (*update)(const cluster_vehicle_fast_t *vehicle);
    void (*set_offline)(bool offline);
} side_page_ops_t;

/* Speed, a large primary value, two smaller values, fuel and water arcs. */
extern const side_page_ops_t data_page_ops;
/* Six configurable values in a 2x3 grid. */
extern const side_page_ops_t gauges_page_ops;
/* Two configurable values plotted over the last 20 seconds. */
extern const side_page_ops_t trends_page_ops;
/* Session maxima with an RPM peak ring; hold to reset. */
extern const side_page_ops_t peaks_page_ops;
/* 0-30, 0-60, 1/8 and 1/4 mile timer; hold to reset. */
extern const side_page_ops_t timer_page_ops;

/* ---- Air Ride ------------------------------------------------------------ */

void air_page_create(lv_obj_t *screen, const side_air_backend_t *backend);
void air_page_set_graphite(bool graphite);
/* Releases any held command and closes the adjustment overlay. */
void air_page_leave(void);
