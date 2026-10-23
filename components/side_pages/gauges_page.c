#include <stdio.h>

#include "dash_fonts.h"
#include "settings_schema.h"
#include "side_pages_internal.h"
#include "vehicle_data.h"

#define GAUGE_COUNT CLUSTER_GAUGE_SLOT_COUNT
#define TILE_WIDTH 150
#define TILE_HEIGHT 88

/* Two columns of three tiles, kept inside the round 480 px panel. */
static const lv_point_t tile_origins[GAUGE_COUNT] = {
    {84, 118}, {246, 118}, {84, 214}, {246, 214}, {84, 310}, {246, 310},
};

static lv_obj_t *page_screen;
static lv_obj_t *heading;
static lv_obj_t *tiles[GAUGE_COUNT];
static lv_obj_t *names[GAUGE_COUNT];
static lv_obj_t *values[GAUGE_COUNT];
static lv_obj_t *units[GAUGE_COUNT];
static uint32_t value_colors[GAUGE_COUNT];
static lv_obj_t *offline_ring;
static uint8_t channels[GAUGE_COUNT];
static const side_palette_t *palette;
static cluster_left_settings_t settings;
static cluster_vehicle_fast_t latest_vehicle;
static bool latest_vehicle_valid;

static void render(const cluster_vehicle_fast_t *vehicle)
{
    char text[16];
    for (size_t index = 0; index < GAUGE_COUNT; ++index) {
        const uint8_t channel = channels[index];
        uint32_t color = palette->text;
        if (vehicle_data_channel_valid(vehicle->valid_channels, channel)) {
            side_channel_format(text, sizeof(text), vehicle, channel);
            color = side_channel_color(vehicle, channel, &settings);
        } else {
            snprintf(text, sizeof(text), "--");
        }
        side_pages_label_set_text(values[index], text);
        side_pages_label_set_text(units[index],
                                  side_channel_unit(vehicle, channel));
        if (value_colors[index] != color) {
            value_colors[index] = color;
            lv_obj_set_style_text_color(values[index], lv_color_hex(color),
                                        LV_PART_MAIN);
        }
    }
}

static void apply_palette(void)
{
    lv_obj_set_style_bg_color(page_screen, lv_color_hex(palette->background),
                              LV_PART_MAIN);
    lv_obj_set_style_text_color(heading, lv_color_hex(palette->accent),
                                LV_PART_MAIN);
    for (size_t index = 0; index < GAUGE_COUNT; ++index) {
        side_panel_style(tiles[index], palette);
        lv_obj_set_style_text_color(names[index], lv_color_hex(palette->dim),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_color(units[index], lv_color_hex(palette->dim),
                                    LV_PART_MAIN);
        value_colors[index] = 0xFFFFFFFFU; /* recolour on the next render */
    }
}

static void gauges_page_create(lv_obj_t *screen)
{
    page_screen = screen;
    palette = side_palette(false);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    heading = side_pages_label_create(screen, "GAUGES", &lv_font_montserrat_14,
                                      palette->accent);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 78);

    for (size_t index = 0; index < GAUGE_COUNT; ++index) {
        lv_obj_t *tile = side_panel_create(screen, tile_origins[index].x,
                                           tile_origins[index].y, TILE_WIDTH,
                                           TILE_HEIGHT);
        tiles[index] = tile;
        names[index] = side_pages_label_create(tile, "--",
                                               &lv_font_montserrat_14,
                                               palette->dim);
        lv_obj_align(names[index], LV_ALIGN_TOP_MID, 0, 8);
        values[index] = side_pages_label_create(tile, "--", &dash_micro_35,
                                                palette->text);
        lv_obj_align(values[index], LV_ALIGN_CENTER, 0, 4);
        units[index] = side_pages_label_create(tile, "",
                                               &lv_font_montserrat_14,
                                               palette->dim);
        lv_obj_align(units[index], LV_ALIGN_BOTTOM_MID, 0, -6);
    }
    offline_ring = side_offline_ring_create(screen);
    apply_palette();
}

static void gauges_page_apply_settings(const cluster_left_settings_t *new_settings,
                                       cluster_display_target_t display)
{
    settings = *new_settings;
    const uint8_t *selected = display == CLUSTER_DISPLAY_RIGHT
        ? settings.right_gauge_channels : settings.left_gauge_channels;
    for (size_t index = 0; index < GAUGE_COUNT; ++index) {
        channels[index] = selected[index];
        side_pages_label_set_text(names[index],
                                  side_channel_name(channels[index]));
    }
    palette = side_palette(settings.theme == CLUSTER_THEME_GRAPHITE);
    apply_palette();
    lv_obj_set_style_border_color(offline_ring,
                                  lv_color_hex(settings.offline_rgb),
                                  LV_PART_MAIN);
    if (latest_vehicle_valid) render(&latest_vehicle);
}

static void gauges_page_update(const cluster_vehicle_fast_t *vehicle)
{
    latest_vehicle = *vehicle;
    latest_vehicle_valid = true;
    render(vehicle);
}

static void gauges_page_set_offline(bool offline)
{
    lv_obj_set_hidden(offline_ring, !offline);
}

const side_page_ops_t gauges_page_ops = {
    .create = gauges_page_create,
    .apply_settings = gauges_page_apply_settings,
    .update = gauges_page_update,
    .set_offline = gauges_page_set_offline,
};
