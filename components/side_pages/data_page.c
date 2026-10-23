#include <stdio.h>
#include <string.h>

#include "dash_fonts.h"
#include "settings_schema.h"
#include "side_pages_internal.h"
#include "vehicle_data.h"

extern const uint8_t side_face_rgb565_start[]
    asm("_binary_side_480_rgb565_bin_start");
extern const uint8_t side_face_rgb565_end[]
    asm("_binary_side_480_rgb565_bin_end");
extern const uint8_t graphite_side_face_rgb565_start[]
    asm("_binary_graphite_side_480_rgb565_bin_start");

#define COLOR_WHITE 0xECECE7U
#define COLOR_ORANGE 0xE81E1EU
#define FUEL_SEGMENT_COUNT 8U
#define WATER_SEGMENT_COUNT 10U
#define VALUE_SLOT_COUNT 3U

typedef struct {
    uint16_t start;
    uint16_t end;
} segment_angle_t;

static const segment_angle_t fuel_angles[FUEL_SEGMENT_COUNT] = {
    {128, 139}, {141, 152}, {155, 166}, {168, 179},
    {181, 192}, {194, 205}, {208, 219}, {221, 232},
};
static const segment_angle_t water_angles[WATER_SEGMENT_COUNT] = {
    {44, 52}, {33, 41}, {22, 31}, {12, 20}, {1, 10},
    {350, 359}, {340, 348}, {329, 338}, {319, 327}, {308, 316},
};

static lv_obj_t *background;
static lv_obj_t *fuel_segments[FUEL_SEGMENT_COUNT];
static lv_obj_t *water_segments[WATER_SEGMENT_COUNT];
static lv_obj_t *graphite_fuel_arc;
static lv_obj_t *graphite_water_arc;
static lv_obj_t *value_labels[VALUE_SLOT_COUNT];
static uint32_t value_colors[VALUE_SLOT_COUNT];
static lv_obj_t *speed_label;
static lv_obj_t *boost_unit_label;
static lv_obj_t *offline_overlay;
static cluster_vehicle_fast_t latest_vehicle;
static bool latest_vehicle_valid;
static cluster_left_settings_t settings = {
    .caution_rgb = COLOR_ORANGE,
    .critical_rgb = COLOR_ORANGE,
    .offline_rgb = 0x84868D,
    .ect_caution = 1950,
    .ect_critical = 2050,
    .iat_caution = 1000,
    .iat_critical = 1100,
    .map_caution = 120,
    .map_critical = 160,
    .battery_caution = 130,
    .battery_critical = 120,
    .value_channels = {
        VEHICLE_DATA_BOOST,
        VEHICLE_DATA_INTAKE_TEMP,
        VEHICLE_DATA_BATTERY,
    },
};

static lv_image_dsc_t face_image = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = 480,
        .h = 480,
        .stride = 960,
    },
    .data = side_face_rgb565_start,
};

static bool graphite_theme(void)
{
    return settings.theme == CLUSTER_THEME_GRAPHITE;
}

static bool channel_valid(const cluster_vehicle_fast_t *vehicle,
                          uint8_t channel)
{
    return vehicle_data_channel_valid(vehicle->valid_channels, channel);
}

static lv_obj_t *make_segment(lv_obj_t *parent, segment_angle_t angle,
                              uint32_t color)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, 452, 452);
    lv_obj_center(arc);
    lv_arc_set_bg_angles(arc, angle.start, angle.end);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_style(arc, NULL, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
    lv_obj_set_clickable(arc, false);
    lv_obj_set_hidden(arc, true);
    return arc;
}

static lv_obj_t *make_graphite_arc(lv_obj_t *parent, uint16_t start,
                                   uint16_t end, lv_arc_mode_t mode)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, 448, 448);
    lv_obj_center(arc);
    lv_arc_set_bg_angles(arc, start, end);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_mode(arc, mode);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(arc, false);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x5EC8F2U),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_hidden(arc, true);
    return arc;
}

static lv_obj_t *make_value(lv_obj_t *parent, int32_t x, int32_t y,
                            int32_t width, int32_t height,
                            const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_WHITE),
                                LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, "--");
    return label;
}

static lv_obj_t *make_unit(lv_obj_t *parent, int32_t x, int32_t y,
                           int32_t width, const char *text)
{
    lv_obj_t *label = make_value(parent, x, y, width, 20,
                                 &lv_font_montserrat_14);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_ORANGE),
                                LV_PART_MAIN);
    lv_label_set_text(label, text);
    return label;
}

/* Classic segments stay hidden under the graphite theme, which shows the
 * same reading on its continuous arcs instead. */
static void set_lit_segments(lv_obj_t **segments, size_t count, size_t lit)
{
    const bool graphite = graphite_theme();
    for (size_t index = 0; index < count; ++index) {
        lv_obj_set_hidden(segments[index], graphite || index >= lit);
    }
}

static size_t water_segment_count(int16_t tenths_c)
{
    const int32_t tenths_f = side_tenths_c_to_tenths_f(tenths_c);
    int32_t segments;
    if (tenths_f <= 600) {
        segments = 0;
    } else if (tenths_f <= 1850) {
        segments = ((tenths_f - 600) * 5 + 1249) / 1250;
    } else {
        segments = 5 + ((tenths_f - 1850) * 5 + 200) / 400;
    }
    if (segments < 0) return 0;
    if (segments > (int32_t)WATER_SEGMENT_COUNT) return WATER_SEGMENT_COUNT;
    return (size_t)segments;
}

static void set_value_color(size_t slot, uint32_t color)
{
    if (value_colors[slot] == color) return;
    value_colors[slot] = color;
    lv_obj_set_style_text_color(value_labels[slot], lv_color_hex(color),
                                LV_PART_MAIN);
}

static void render(const cluster_vehicle_fast_t *vehicle)
{
    char text[16];
    if (channel_valid(vehicle, VEHICLE_DATA_SPEED)) {
        const uint32_t mph = (uint32_t)(
            ((uint64_t)vehicle->speed_tenths_kph * 621371U + 5000000U) /
            10000000U);
        snprintf(text, sizeof(text), "%lu", (unsigned long)mph);
        side_pages_label_set_text(speed_label, text);
    } else {
        side_pages_label_set_text(speed_label, "--");
    }

    const bool vacuum = channel_valid(vehicle, VEHICLE_DATA_BOOST) &&
                        vehicle->boost_tenths_kpa < 0;
    side_pages_label_set_text(boost_unit_label, vacuum ? "inHg" : "PSI");

    for (size_t slot = 0; slot < VALUE_SLOT_COUNT; ++slot) {
        const uint8_t channel = settings.value_channels[slot];
        if (channel_valid(vehicle, channel)) {
            side_channel_format(text, sizeof(text), vehicle, channel);
            set_value_color(slot, side_channel_color(vehicle, channel, &settings));
        } else {
            snprintf(text, sizeof(text), "--");
            set_value_color(slot, COLOR_WHITE);
        }
        side_pages_label_set_text(value_labels[slot], text);
    }

    size_t water_lit = 0U;
    if (channel_valid(vehicle, VEHICLE_DATA_COOLANT_TEMP)) {
        water_lit = water_segment_count(vehicle->coolant_tenths_c);
    }
    set_lit_segments(water_segments, WATER_SEGMENT_COUNT, water_lit);
    lv_arc_set_value(graphite_water_arc, (int32_t)(water_lit * 10U));

    if (channel_valid(vehicle, VEHICLE_DATA_FUEL_LEVEL)) {
        size_t fuel_lit =
            ((size_t)vehicle->fuel_level_tenths_percent * 8U + 999U) / 1000U;
        if (fuel_lit > FUEL_SEGMENT_COUNT) fuel_lit = FUEL_SEGMENT_COUNT;
        lv_obj_set_style_arc_color(
            fuel_segments[0],
            lv_color_hex(fuel_lit <= 1U ? settings.critical_rgb : COLOR_WHITE),
            LV_PART_MAIN);
        set_lit_segments(fuel_segments, FUEL_SEGMENT_COUNT, fuel_lit);
        lv_arc_set_value(graphite_fuel_arc,
                         vehicle->fuel_level_tenths_percent / 10U);
    } else {
        set_lit_segments(fuel_segments, FUEL_SEGMENT_COUNT, 0);
        lv_arc_set_value(graphite_fuel_arc, 0);
    }
}

static void data_page_create(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    face_image.data_size = (uint32_t)(side_face_rgb565_end -
                                      side_face_rgb565_start);
    background = lv_image_create(screen);
    lv_image_set_src(background, &face_image);
    lv_obj_center(background);
    lv_obj_set_clickable(background, false);

    for (size_t index = 0; index < FUEL_SEGMENT_COUNT; ++index) {
        fuel_segments[index] = make_segment(screen, fuel_angles[index],
                                            COLOR_WHITE);
    }
    for (size_t index = 0; index < WATER_SEGMENT_COUNT; ++index) {
        water_segments[index] = make_segment(
            screen, water_angles[index],
            index >= 8 ? settings.caution_rgb : COLOR_WHITE);
    }
    graphite_fuel_arc = make_graphite_arc(screen, 130, 230, LV_ARC_MODE_NORMAL);
    graphite_water_arc = make_graphite_arc(screen, 310, 50, LV_ARC_MODE_REVERSE);

    speed_label = make_value(screen, 160, 42, 160, 52, &dash_micro_35);
    make_unit(screen, 190, 92, 100, "MPH");

    value_labels[0] = make_value(screen, 140, 164, 200, 70, &dash_micro_56);
    boost_unit_label = make_unit(screen, 190, 232, 100, "PSI");
    value_labels[1] = make_value(screen, 126, 294, 104, 36, &dash_micro_28);
    value_labels[2] = make_value(screen, 250, 294, 104, 36, &dash_micro_28);
    for (size_t slot = 0; slot < VALUE_SLOT_COUNT; ++slot) {
        value_colors[slot] = COLOR_WHITE;
    }

    offline_overlay = lv_obj_create(screen);
    lv_obj_set_size(offline_overlay, 472, 472);
    lv_obj_center(offline_overlay);
    lv_obj_set_style_radius(offline_overlay, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(offline_overlay, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(offline_overlay, 6, LV_PART_MAIN);
    lv_obj_set_style_border_color(offline_overlay,
                                  lv_color_hex(settings.offline_rgb),
                                  LV_PART_MAIN);
    lv_obj_set_clickable(offline_overlay, false);
}

static void data_page_apply_settings(const cluster_left_settings_t *new_settings,
                                     cluster_display_target_t display)
{
    settings = *new_settings;
    if (display == CLUSTER_DISPLAY_RIGHT) {
        memcpy(settings.value_channels, new_settings->right_value_channels,
               sizeof(settings.value_channels));
    }
    const bool graphite = graphite_theme();
    face_image.data = graphite ? graphite_side_face_rgb565_start
                               : side_face_rgb565_start;
    lv_image_set_src(background, &face_image);
    lv_obj_invalidate(background);
    lv_obj_set_hidden(graphite_fuel_arc, !graphite);
    lv_obj_set_hidden(graphite_water_arc, !graphite);
    lv_obj_set_style_border_color(offline_overlay,
                                  lv_color_hex(settings.offline_rgb),
                                  LV_PART_MAIN);
    for (size_t index = 8; index < WATER_SEGMENT_COUNT; ++index) {
        lv_obj_set_style_arc_color(water_segments[index],
                                   lv_color_hex(settings.caution_rgb),
                                   LV_PART_MAIN);
    }
    if (latest_vehicle_valid) {
        render(&latest_vehicle);
    } else {
        set_lit_segments(fuel_segments, FUEL_SEGMENT_COUNT, 0);
        set_lit_segments(water_segments, WATER_SEGMENT_COUNT, 0);
    }
}

static void data_page_update(const cluster_vehicle_fast_t *vehicle)
{
    latest_vehicle = *vehicle;
    latest_vehicle_valid = true;
    render(vehicle);
}

static void data_page_set_offline(bool offline)
{
    lv_obj_set_hidden(offline_overlay, !offline);
}

const side_page_ops_t data_page_ops = {
    .create = data_page_create,
    .apply_settings = data_page_apply_settings,
    .update = data_page_update,
    .set_offline = data_page_set_offline,
};
