#include "left_dashboard.h"

#include <stdio.h>
#include <string.h>

#include "climate_display.h"
#include "dash_fonts.h"
#include "settings_schema.h"
#include "side_display.h"
#include "side_speedometer.h"
#include "vehicle_data.h"

extern const uint8_t side_face_rgb565_start[]
    asm("_binary_side_480_rgb565_bin_start");
extern const uint8_t side_face_rgb565_end[]
    asm("_binary_side_480_rgb565_bin_end");
extern const uint8_t graphite_side_face_rgb565_start[]
    asm("_binary_graphite_side_480_rgb565_bin_start");

#define LEFT_COLOR_WHITE 0xECECE7U
#define LEFT_COLOR_ORANGE 0xE81E1EU
#define FUEL_SEGMENT_COUNT 8U
#define WATER_SEGMENT_COUNT 10U
#define VALUE_SLOT_COUNT 3U

typedef enum {
    PAGE_DATA,
    PAGE_SPEEDOMETER,
    PAGE_CLIMATE,
} left_page_t;

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

static lv_obj_t *fuel_segments[FUEL_SEGMENT_COUNT];
static lv_obj_t *water_segments[WATER_SEGMENT_COUNT];
static lv_obj_t *graphite_fuel_arc;
static lv_obj_t *graphite_water_arc;
static lv_obj_t *value_labels[VALUE_SLOT_COUNT];
static uint32_t value_colors[VALUE_SLOT_COUNT];
static lv_obj_t *speed_label;
static lv_obj_t *boost_unit_label;
static lv_obj_t *offline_overlay;
static lv_obj_t *dashboard_screen;
static lv_obj_t *dashboard_background;
static lv_obj_t *speedometer_screen;
static lv_obj_t *climate_screen;
static cluster_vehicle_fast_t latest_vehicle;
static bool latest_vehicle_valid;
static bool display_offline = true;
static left_page_t page = PAGE_DATA;
static bool page_transitioning;
static cluster_left_settings_t current_settings = {
    .brightness = 95,
    .caution_rgb = LEFT_COLOR_ORANGE,
    .critical_rgb = LEFT_COLOR_ORANGE,
    .offline_rgb = 0x84868D,
    .tach_rgb = 0xEC5C28,
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

static lv_image_dsc_t side_face_image = {
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
    return current_settings.theme == CLUSTER_THEME_GRAPHITE;
}

static bool channel_valid(const cluster_vehicle_fast_t *vehicle,
                          uint8_t channel)
{
    return vehicle_data_channel_valid(vehicle->valid_channels, channel);
}

static void label_set_text_if_changed(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
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
    lv_obj_set_style_text_color(label, lv_color_hex(LEFT_COLOR_WHITE),
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
    lv_obj_set_style_text_color(label, lv_color_hex(LEFT_COLOR_ORANGE),
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
    const int32_t tenths_f = ((int32_t)tenths_c * 9) / 5 + 320;
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

static int32_t tenths_c_to_tenths_f(int16_t tenths_c)
{
    return ((int32_t)tenths_c * 9) / 5 + 320;
}

static void format_channel(char *output, size_t output_size,
                           const cluster_vehicle_fast_t *vehicle,
                           uint8_t channel)
{
    switch ((vehicle_data_channel_t)channel) {
    case VEHICLE_DATA_RPM:
        snprintf(output, output_size, "%u", vehicle->rpm);
        break;
    case VEHICLE_DATA_SPEED:
        snprintf(output, output_size, "%u", vehicle->speed_tenths_kph / 10U);
        break;
    case VEHICLE_DATA_BOOST: {
        /* Vacuum reads in inHg, boost in PSI, both to one decimal. */
        const int32_t gauge_tenths_kpa = vehicle->boost_tenths_kpa;
        const int32_t display_tenths = gauge_tenths_kpa < 0
            ? (gauge_tenths_kpa * 2953 - 5000) / 10000
            : (gauge_tenths_kpa * 14504 + 50000) / 100000;
        const int32_t magnitude = display_tenths < 0
            ? -display_tenths : display_tenths;
        snprintf(output, output_size, "%s%ld.%ld",
                 display_tenths < 0 ? "-" : "",
                 (long)(magnitude / 10), (long)(magnitude % 10));
        break;
    }
    case VEHICLE_DATA_COOLANT_TEMP:
        snprintf(output, output_size, "%ld",
                 (long)(tenths_c_to_tenths_f(vehicle->coolant_tenths_c) / 10));
        break;
    case VEHICLE_DATA_INTAKE_TEMP:
        snprintf(output, output_size, "%ld",
                 (long)(tenths_c_to_tenths_f(vehicle->intake_tenths_c) / 10));
        break;
    case VEHICLE_DATA_AFR:
        snprintf(output, output_size, "%u.%u", vehicle->afr_thousandths / 1000U,
                 (vehicle->afr_thousandths % 1000U) / 100U);
        break;
    case VEHICLE_DATA_OIL_PRESSURE:
        snprintf(output, output_size, "%lu", (unsigned long)
                 (((uint32_t)vehicle->oil_pressure_tenths_kpa * 14504U) /
                  1000000U));
        break;
    case VEHICLE_DATA_FUEL_PRESSURE:
        snprintf(output, output_size, "%lu", (unsigned long)
                 (((uint32_t)vehicle->fuel_pressure_tenths_kpa * 14504U) /
                  1000000U));
        break;
    case VEHICLE_DATA_BATTERY:
        snprintf(output, output_size, "%u.%u", vehicle->battery_mv / 1000U,
                 (vehicle->battery_mv % 1000U) / 100U);
        break;
    case VEHICLE_DATA_THROTTLE:
        snprintf(output, output_size, "%u", vehicle->throttle_tenths_percent / 10U);
        break;
    case VEHICLE_DATA_FUEL_LEVEL:
        snprintf(output, output_size, "%u", vehicle->fuel_level_tenths_percent / 10U);
        break;
    default:
        snprintf(output, output_size, "--");
        break;
    }
}

static uint32_t channel_color(const cluster_vehicle_fast_t *vehicle,
                              uint8_t channel)
{
    int32_t value;
    int32_t caution;
    int32_t critical;
    bool lower_is_worse = false;

    switch ((vehicle_data_channel_t)channel) {
    case VEHICLE_DATA_BOOST:
        value = ((int32_t)vehicle->boost_tenths_kpa * 14504) / 100000;
        caution = current_settings.map_caution;
        critical = current_settings.map_critical;
        break;
    case VEHICLE_DATA_COOLANT_TEMP:
        value = tenths_c_to_tenths_f(vehicle->coolant_tenths_c);
        caution = current_settings.ect_caution;
        critical = current_settings.ect_critical;
        break;
    case VEHICLE_DATA_INTAKE_TEMP:
        value = tenths_c_to_tenths_f(vehicle->intake_tenths_c);
        caution = current_settings.iat_caution;
        critical = current_settings.iat_critical;
        break;
    case VEHICLE_DATA_BATTERY:
        value = (vehicle->battery_mv + 50U) / 100U;
        caution = current_settings.battery_caution;
        critical = current_settings.battery_critical;
        lower_is_worse = true;
        break;
    default:
        return LEFT_COLOR_WHITE;
    }

    if (lower_is_worse) {
        if (value <= critical) return current_settings.critical_rgb;
        if (value <= caution) return current_settings.caution_rgb;
    } else {
        if (value >= critical) return current_settings.critical_rgb;
        if (value >= caution) return current_settings.caution_rgb;
    }
    return LEFT_COLOR_WHITE;
}

static void set_value_color(size_t slot, uint32_t color)
{
    if (value_colors[slot] == color) return;
    value_colors[slot] = color;
    lv_obj_set_style_text_color(value_labels[slot], lv_color_hex(color),
                                LV_PART_MAIN);
}

static void dashboard_update_locked(const cluster_vehicle_fast_t *vehicle)
{
    char text[16];
    if (channel_valid(vehicle, VEHICLE_DATA_SPEED)) {
        const uint32_t mph = (uint32_t)(
            ((uint64_t)vehicle->speed_tenths_kph * 621371U + 5000000U) /
            10000000U);
        snprintf(text, sizeof(text), "%lu", (unsigned long)mph);
        label_set_text_if_changed(speed_label, text);
    } else {
        label_set_text_if_changed(speed_label, "--");
    }

    const bool vacuum = channel_valid(vehicle, VEHICLE_DATA_BOOST) &&
                        vehicle->boost_tenths_kpa < 0;
    label_set_text_if_changed(boost_unit_label, vacuum ? "inHg" : "PSI");

    for (size_t slot = 0; slot < VALUE_SLOT_COUNT; ++slot) {
        const uint8_t channel = current_settings.value_channels[slot];
        if (channel_valid(vehicle, channel)) {
            format_channel(text, sizeof(text), vehicle, channel);
            set_value_color(slot, channel_color(vehicle, channel));
        } else {
            snprintf(text, sizeof(text), "--");
            set_value_color(slot, LEFT_COLOR_WHITE);
        }
        label_set_text_if_changed(value_labels[slot], text);
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
            lv_color_hex(fuel_lit <= 1U ? current_settings.critical_rgb :
                                        LEFT_COLOR_WHITE),
            LV_PART_MAIN);
        set_lit_segments(fuel_segments, FUEL_SEGMENT_COUNT, fuel_lit);
        lv_arc_set_value(graphite_fuel_arc,
                         vehicle->fuel_level_tenths_percent / 10U);
    } else {
        set_lit_segments(fuel_segments, FUEL_SEGMENT_COUNT, 0);
        lv_arc_set_value(graphite_fuel_arc, 0);
    }
}

static void update_offline_locked(void)
{
    climate_display_set_offline(display_offline);
    side_speedometer_set_offline(display_offline, current_settings.offline_rgb);
    lv_obj_set_hidden(offline_overlay, !display_offline);
}

static void show_page_locked(left_page_t next)
{
    page = next;
    if (page == PAGE_DATA && latest_vehicle_valid) {
        dashboard_update_locked(&latest_vehicle);
    }
}

static lv_obj_t *page_screen(left_page_t target)
{
    switch (target) {
    case PAGE_SPEEDOMETER: return speedometer_screen;
    case PAGE_CLIMATE: return climate_screen;
    default: return dashboard_screen;
    }
}

static void page_loaded_event(lv_event_t *event)
{
    (void)event;
    page_transitioning = false;
}

static void page_swipe_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (page_transitioning ||
        (code != LV_EVENT_GESTURE_LEFT && code != LV_EVENT_GESTURE_RIGHT)) {
        return;
    }
    show_page_locked(page == PAGE_SPEEDOMETER ? PAGE_DATA : PAGE_SPEEDOMETER);
    page_transitioning = true;
    lv_screen_load_anim(page_screen(page),
                        code == LV_EVENT_GESTURE_LEFT
                            ? LV_SCREEN_LOAD_ANIM_MOVE_LEFT
                            : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT,
                        250U, 0U, false);
}

static void add_swipe_events(lv_obj_t *screen)
{
    lv_obj_add_event_cb(screen, page_swipe_event, LV_EVENT_GESTURE_LEFT, NULL);
    lv_obj_add_event_cb(screen, page_swipe_event, LV_EVENT_GESTURE_RIGHT, NULL);
    lv_obj_add_event_cb(screen, page_loaded_event, LV_EVENT_SCREEN_LOADED,
                        NULL);
}

void left_dashboard_create(lv_obj_t *screen, void *context)
{
    (void)context;
    dashboard_screen = screen;
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    side_face_image.data_size = (uint32_t)(side_face_rgb565_end -
                                           side_face_rgb565_start);
    dashboard_background = lv_image_create(screen);
    lv_image_set_src(dashboard_background, &side_face_image);
    lv_obj_center(dashboard_background);
    lv_obj_set_clickable(dashboard_background, false);

    for (size_t index = 0; index < FUEL_SEGMENT_COUNT; ++index) {
        fuel_segments[index] = make_segment(screen, fuel_angles[index],
                                            LEFT_COLOR_WHITE);
    }
    for (size_t index = 0; index < WATER_SEGMENT_COUNT; ++index) {
        water_segments[index] = make_segment(
            screen, water_angles[index],
            index >= 8 ? current_settings.caution_rgb : LEFT_COLOR_WHITE);
    }
    graphite_fuel_arc = make_graphite_arc(
        screen, 130, 230, LV_ARC_MODE_NORMAL);
    graphite_water_arc = make_graphite_arc(
        screen, 310, 50, LV_ARC_MODE_REVERSE);

    speed_label = make_value(screen, 160, 42, 160, 52, &dash_micro_35);
    make_unit(screen, 190, 92, 100, "MPH");

    value_labels[0] = make_value(screen, 140, 164, 200, 70, &dash_micro_56);
    boost_unit_label = make_unit(screen, 190, 232, 100, "PSI");
    value_labels[1] = make_value(screen, 126, 294, 104, 36, &dash_micro_28);
    value_labels[2] = make_value(screen, 250, 294, 104, 36, &dash_micro_28);
    for (size_t slot = 0; slot < VALUE_SLOT_COUNT; ++slot) {
        value_colors[slot] = LEFT_COLOR_WHITE;
    }

    offline_overlay = lv_obj_create(screen);
    lv_obj_set_size(offline_overlay, 472, 472);
    lv_obj_center(offline_overlay);
    lv_obj_set_style_radius(offline_overlay, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(offline_overlay, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(offline_overlay, 6, LV_PART_MAIN);
    lv_obj_set_style_border_color(offline_overlay,
                                  lv_color_hex(current_settings.offline_rgb),
                                  LV_PART_MAIN);
    lv_obj_set_clickable(offline_overlay, false);

    speedometer_screen = lv_obj_create(NULL);
    side_speedometer_create(speedometer_screen);
    side_speedometer_set_accent(current_settings.tach_rgb);
    climate_screen = climate_display_create();
    add_swipe_events(dashboard_screen);
    add_swipe_events(speedometer_screen);
    update_offline_locked();
}

void left_dashboard_update(const cluster_vehicle_fast_t *vehicle)
{
    if (vehicle == NULL) return;
    side_speedometer_update(vehicle->speed_tenths_kph,
                            channel_valid(vehicle, VEHICLE_DATA_SPEED));
    if (side_display_lock(100) != ESP_OK) return;
    latest_vehicle = *vehicle;
    latest_vehicle_valid = true;
    if (page == PAGE_DATA) dashboard_update_locked(vehicle);
    side_display_unlock();
}

void left_dashboard_apply_settings(const cluster_left_settings_t *settings)
{
    if (settings == NULL) return;
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        side_display_set_brightness(settings->brightness));
    if (side_display_lock(100) != ESP_OK) return;
    current_settings = *settings;
    const bool graphite = graphite_theme();
    side_face_image.data = graphite ? graphite_side_face_rgb565_start
                                    : side_face_rgb565_start;
    lv_image_set_src(dashboard_background, &side_face_image);
    lv_obj_invalidate(dashboard_background);
    side_speedometer_set_graphite(graphite);
    side_speedometer_set_accent(current_settings.tach_rgb);
    climate_display_set_graphite(graphite);
    lv_obj_set_hidden(graphite_fuel_arc, !graphite);
    lv_obj_set_hidden(graphite_water_arc, !graphite);
    lv_obj_set_style_border_color(offline_overlay,
                                  lv_color_hex(current_settings.offline_rgb),
                                  LV_PART_MAIN);
    for (size_t index = 8; index < WATER_SEGMENT_COUNT; ++index) {
        lv_obj_set_style_arc_color(water_segments[index],
                                   lv_color_hex(current_settings.caution_rgb),
                                   LV_PART_MAIN);
    }
    if (latest_vehicle_valid) {
        dashboard_update_locked(&latest_vehicle);
    } else {
        set_lit_segments(fuel_segments, FUEL_SEGMENT_COUNT, 0);
        set_lit_segments(water_segments, WATER_SEGMENT_COUNT, 0);
    }
    update_offline_locked();
    side_display_unlock();
}

void left_dashboard_apply_display_mode(cluster_display_view_t view)
{
    left_page_t next;
    switch (view) {
    case CLUSTER_VIEW_DATA: next = PAGE_DATA; break;
    case CLUSTER_VIEW_SPEEDOMETER: next = PAGE_SPEEDOMETER; break;
    case CLUSTER_VIEW_CLIMATE: next = PAGE_CLIMATE; break;
    default: return;
    }
    if (side_display_lock(100) != ESP_OK) return;
    show_page_locked(next);
    lv_screen_load(page_screen(page));
    side_display_unlock();
}

void left_dashboard_set_offline(bool offline)
{
    if (side_display_lock(100) != ESP_OK) return;
    display_offline = offline;
    update_offline_locked();
    side_display_unlock();
}
