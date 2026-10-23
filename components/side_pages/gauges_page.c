#include <stdio.h>

#include "dash_fonts.h"
#include "settings_schema.h"
#include "side_pages_internal.h"
#include "vehicle_data.h"

#define GAUGE_COUNT CLUSTER_GAUGE_SLOT_COUNT
#define TILE_WIDTH 150
#define TILE_HEIGHT 88
#define COLOR_WHITE 0xECECE7U

/* Two columns of three tiles, kept inside the round 480 px panel. */
static const lv_point_t tile_origins[GAUGE_COUNT] = {
    {84, 118}, {246, 118}, {84, 214}, {246, 214}, {84, 310}, {246, 310},
};

typedef struct {
    uint32_t background;
    uint32_t tile;
    uint32_t border;
    uint32_t title;
    uint32_t heading;
} gauges_theme_t;

static const gauges_theme_t classic_theme = {
    .background = 0x000000, .tile = 0x0C0C0D, .border = 0x2C2C2F,
    .title = 0x9A9A96, .heading = 0xE81E1E,
};
static const gauges_theme_t graphite_theme = {
    .background = 0x0B0E11, .tile = 0x12171C, .border = 0x26323C,
    .title = 0x8A97A3, .heading = 0x5EC8F2,
};

static lv_obj_t *page_screen;
static lv_obj_t *heading;
static lv_obj_t *tiles[GAUGE_COUNT];
static lv_obj_t *names[GAUGE_COUNT];
static lv_obj_t *values[GAUGE_COUNT];
static lv_obj_t *units[GAUGE_COUNT];
static uint32_t value_colors[GAUGE_COUNT];
static lv_obj_t *offline_border;
static uint8_t channels[GAUGE_COUNT];
static cluster_left_settings_t settings;
static cluster_vehicle_fast_t latest_vehicle;
static bool latest_vehicle_valid;

static void apply_theme(const gauges_theme_t *theme);

static void render(const cluster_vehicle_fast_t *vehicle)
{
    char text[16];
    for (size_t index = 0; index < GAUGE_COUNT; ++index) {
        const uint8_t channel = channels[index];
        uint32_t color = COLOR_WHITE;
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

void gauges_page_create(lv_obj_t *screen)
{
    page_screen = screen;
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    heading = side_pages_label_create(screen, "GAUGES", &lv_font_montserrat_14,
                                      classic_theme.heading);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 78);

    for (size_t index = 0; index < GAUGE_COUNT; ++index) {
        lv_obj_t *tile = lv_obj_create(screen);
        tiles[index] = tile;
        lv_obj_set_pos(tile, tile_origins[index].x, tile_origins[index].y);
        lv_obj_set_size(tile, TILE_WIDTH, TILE_HEIGHT);
        lv_obj_set_style_radius(tile, 12, LV_PART_MAIN);
        lv_obj_set_style_border_width(tile, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_all(tile, 0, LV_PART_MAIN);
        lv_obj_set_scrollable(tile, false);
        lv_obj_set_clickable(tile, false);

        names[index] = side_pages_label_create(tile, "--",
                                               &lv_font_montserrat_14,
                                               classic_theme.title);
        lv_obj_align(names[index], LV_ALIGN_TOP_MID, 0, 8);
        values[index] = side_pages_label_create(tile, "--", &dash_micro_35,
                                                COLOR_WHITE);
        lv_obj_align(values[index], LV_ALIGN_CENTER, 0, 4);
        units[index] = side_pages_label_create(tile, "",
                                               &lv_font_montserrat_14,
                                               classic_theme.title);
        lv_obj_align(units[index], LV_ALIGN_BOTTOM_MID, 0, -6);
        value_colors[index] = COLOR_WHITE;
    }

    offline_border = lv_obj_create(screen);
    lv_obj_set_size(offline_border, 472, 472);
    lv_obj_center(offline_border);
    lv_obj_set_style_radius(offline_border, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(offline_border, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(offline_border, 6, LV_PART_MAIN);
    lv_obj_set_style_border_color(offline_border, lv_color_hex(0x84868DU),
                                  LV_PART_MAIN);
    lv_obj_set_clickable(offline_border, false);
    apply_theme(&classic_theme);
}

void gauges_page_apply_settings(const cluster_left_settings_t *new_settings,
                                const uint8_t *new_channels)
{
    settings = *new_settings;
    for (size_t index = 0; index < GAUGE_COUNT; ++index) {
        channels[index] = new_channels[index];
        side_pages_label_set_text(names[index],
                                  side_channel_name(channels[index]));
    }

    apply_theme(settings.theme == CLUSTER_THEME_GRAPHITE ? &graphite_theme
                                                         : &classic_theme);
    lv_obj_set_style_border_color(offline_border,
                                  lv_color_hex(settings.offline_rgb),
                                  LV_PART_MAIN);
    if (latest_vehicle_valid) render(&latest_vehicle);
}

static void apply_theme(const gauges_theme_t *theme)
{
    lv_obj_set_style_bg_color(page_screen, lv_color_hex(theme->background),
                              LV_PART_MAIN);
    lv_obj_set_style_text_color(heading, lv_color_hex(theme->heading),
                                LV_PART_MAIN);
    for (size_t index = 0; index < GAUGE_COUNT; ++index) {
        lv_obj_set_style_bg_color(tiles[index], lv_color_hex(theme->tile),
                                  LV_PART_MAIN);
        lv_obj_set_style_border_color(tiles[index],
                                      lv_color_hex(theme->border), LV_PART_MAIN);
        lv_obj_set_style_text_color(names[index], lv_color_hex(theme->title),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_color(units[index], lv_color_hex(theme->title),
                                    LV_PART_MAIN);
    }
}

void gauges_page_update(const cluster_vehicle_fast_t *vehicle)
{
    latest_vehicle = *vehicle;
    latest_vehicle_valid = true;
    render(vehicle);
}

void gauges_page_set_offline(bool offline)
{
    lv_obj_set_hidden(offline_border, !offline);
}
