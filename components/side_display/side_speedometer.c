#include "side_speedometer.h"

#include <math.h>
#include <stdatomic.h>
#include <stddef.h>

#include "gauge_filter.h"

extern const uint8_t speedo_face_rgb565_start[]
    asm("_binary_speedo_480_rgb565_bin_start");
extern const uint8_t speedo_face_rgb565_end[]
    asm("_binary_speedo_480_rgb565_bin_end");
extern const uint8_t graphite_speedo_face_rgb565_start[]
    asm("_binary_graphite_speedo_480_rgb565_bin_start");

#define SPEEDOMETER_GLOW_LAYER_COUNT 3U
#define SPEEDOMETER_MAX_MPH 160U
#define SPEEDOMETER_SCALE_MAX (SPEEDOMETER_MAX_MPH * 10U)
#define SPEEDOMETER_NEEDLE_LENGTH 218
#define SPEEDOMETER_FRAME_MS 16U
#define SPEEDOMETER_FILTER_MS 90.0f
#define SPEEDOMETER_GRAPHITE_RGB 0x5EC8F2U

static const lv_coord_t classic_diameters[SPEEDOMETER_GLOW_LAYER_COUNT] = {
    476, 452, 428,
};
static const lv_opa_t classic_opacities[SPEEDOMETER_GLOW_LAYER_COUNT] = {
    230, 120, 40,
};

static lv_obj_t *speedometer_screen;
static lv_obj_t *speedometer_scale;
static lv_obj_t *speedometer_needle;
static lv_obj_t *speedometer_background;
static lv_obj_t *speedometer_glow_layers[SPEEDOMETER_GLOW_LAYER_COUNT];
static lv_obj_t *speedometer_offline_border;
static gauge_filter_t speed_filter;
static atomic_uint_fast16_t target_tenths_mph = ATOMIC_VAR_INIT(0U);
static uint16_t rendered_tenths_mph;
static bool speed_rendered;
static bool graphite_theme;
static uint32_t accent_rgb = 0xEC5C28U;

static lv_image_dsc_t speedometer_face_image = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = 480,
        .h = 480,
        .stride = 960,
    },
    .data = speedo_face_rgb565_start,
};

static void apply_accent(void)
{
    const lv_color_t color = lv_color_hex(
        graphite_theme ? SPEEDOMETER_GRAPHITE_RGB : accent_rgb);
    for (size_t index = 0; index < SPEEDOMETER_GLOW_LAYER_COUNT; ++index) {
        lv_obj_set_style_arc_color(speedometer_glow_layers[index], color,
                                   LV_PART_INDICATOR);
    }
    lv_obj_set_style_line_color(speedometer_needle, color, LV_PART_MAIN);
}

/* Runs every frame; only moves the needle while the speedometer is shown. */
static void speedometer_frame(lv_timer_t *timer)
{
    (void)timer;
    if (lv_screen_active() != speedometer_screen) return;

    const float filtered = gauge_filter_update(
        &speed_filter,
        (float)atomic_load_explicit(&target_tenths_mph, memory_order_relaxed),
        lv_tick_get(), SPEEDOMETER_FILTER_MS);
    const uint16_t tenths_mph = (uint16_t)lroundf(filtered);
    if (speed_rendered && tenths_mph == rendered_tenths_mph) return;

    for (size_t index = 0; index < SPEEDOMETER_GLOW_LAYER_COUNT; ++index) {
        lv_arc_set_value(speedometer_glow_layers[index], tenths_mph);
    }
    lv_scale_set_line_needle_value(speedometer_scale, speedometer_needle,
                                   SPEEDOMETER_NEEDLE_LENGTH, tenths_mph);
    rendered_tenths_mph = tenths_mph;
    speed_rendered = true;
}

void side_speedometer_create(lv_obj_t *screen)
{
    speedometer_screen = screen;
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    speedometer_face_image.data_size =
        (uint32_t)(speedo_face_rgb565_end - speedo_face_rgb565_start);
    speedometer_background = lv_image_create(screen);
    lv_image_set_src(speedometer_background, &speedometer_face_image);
    lv_obj_center(speedometer_background);
    lv_obj_set_clickable(speedometer_background, false);

    for (size_t index = 0; index < SPEEDOMETER_GLOW_LAYER_COUNT; ++index) {
        lv_obj_t *glow = lv_arc_create(screen);
        speedometer_glow_layers[index] = glow;
        lv_obj_set_size(glow, classic_diameters[index],
                        classic_diameters[index]);
        lv_obj_center(glow);
        lv_arc_set_bg_angles(glow, 140, 40);
        lv_arc_set_range(glow, 0, SPEEDOMETER_SCALE_MAX);
        lv_arc_set_value(glow, 0);
        lv_obj_remove_style(glow, NULL, LV_PART_KNOB);
        lv_obj_set_clickable(glow, false);
        lv_obj_set_style_arc_opa(glow, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_width(glow, 6, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(glow, classic_opacities[index],
                                 LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(glow, true, LV_PART_INDICATOR);
    }

    speedometer_scale = lv_scale_create(screen);
    lv_obj_set_size(speedometer_scale, 480, 480);
    lv_obj_center(speedometer_scale);
    lv_scale_set_mode(speedometer_scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(speedometer_scale, 0, SPEEDOMETER_SCALE_MAX);
    lv_scale_set_angle_range(speedometer_scale, 260);
    lv_scale_set_rotation(speedometer_scale, 140);
    lv_scale_set_total_tick_count(speedometer_scale, 0);
    lv_scale_set_label_show(speedometer_scale, false);
    lv_obj_set_style_bg_opa(speedometer_scale, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(speedometer_scale, 0, LV_PART_MAIN);

    speedometer_needle = lv_line_create(speedometer_scale);
    lv_obj_set_style_line_width(speedometer_needle, 5, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(speedometer_needle, true, LV_PART_MAIN);
    lv_scale_set_line_needle_value(speedometer_scale, speedometer_needle,
                                   SPEEDOMETER_NEEDLE_LENGTH, 0);
    apply_accent();

    lv_obj_t *hub = lv_obj_create(screen);
    lv_obj_set_size(hub, 60, 60);
    lv_obj_center(hub);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hub, lv_color_hex(0x0E0E0FU), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(hub, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(hub, false);
    lv_obj_set_clickable(hub, false);

    speedometer_offline_border = lv_obj_create(screen);
    lv_obj_set_size(speedometer_offline_border, 472, 472);
    lv_obj_center(speedometer_offline_border);
    lv_obj_set_style_radius(speedometer_offline_border, LV_RADIUS_CIRCLE,
                            LV_PART_MAIN);
    lv_obj_set_style_bg_opa(speedometer_offline_border, LV_OPA_TRANSP,
                            LV_PART_MAIN);
    lv_obj_set_style_border_width(speedometer_offline_border, 6, LV_PART_MAIN);
    lv_obj_set_style_border_color(speedometer_offline_border,
                                  lv_color_hex(0x84868DU), LV_PART_MAIN);
    lv_obj_set_clickable(speedometer_offline_border, false);
    lv_obj_set_hidden(speedometer_offline_border, true);

    lv_timer_create(speedometer_frame, SPEEDOMETER_FRAME_MS, NULL);
}

void side_speedometer_set_graphite(bool graphite)
{
    graphite_theme = graphite;
    speedometer_face_image.data = graphite ? graphite_speedo_face_rgb565_start
                                           : speedo_face_rgb565_start;
    lv_image_set_src(speedometer_background, &speedometer_face_image);
    lv_obj_set_hidden(speedometer_needle, graphite);
    for (size_t index = 0; index < SPEEDOMETER_GLOW_LAYER_COUNT; ++index) {
        lv_obj_t *glow = speedometer_glow_layers[index];
        const lv_coord_t diameter = graphite ? 424 : classic_diameters[index];
        lv_obj_set_hidden(glow, graphite && index != 0U);
        lv_obj_set_size(glow, diameter, diameter);
        lv_arc_set_bg_angles(glow, graphite ? 135 : 140, graphite ? 45 : 40);
        lv_obj_set_style_arc_width(glow, graphite ? 16 : 6, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(glow,
                                 graphite ? LV_OPA_COVER
                                          : classic_opacities[index],
                                 LV_PART_INDICATOR);
    }
    apply_accent();
    lv_obj_invalidate(speedometer_background);
}

void side_speedometer_update(uint16_t speed_tenths_kph, bool valid)
{
    uint32_t tenths_mph = 0U;
    if (valid) {
        tenths_mph = (uint32_t)(((uint64_t)speed_tenths_kph * 621371U +
                                 500000U) / 1000000U);
        if (tenths_mph > SPEEDOMETER_SCALE_MAX) {
            tenths_mph = SPEEDOMETER_SCALE_MAX;
        }
    }
    atomic_store_explicit(&target_tenths_mph, (uint16_t)tenths_mph,
                          memory_order_relaxed);
}

void side_speedometer_set_accent(uint32_t rgb)
{
    rgb &= 0x00FFFFFFU;
    if (rgb == accent_rgb) return;
    accent_rgb = rgb;
    apply_accent();
}

void side_speedometer_set_offline(bool offline, uint32_t offline_rgb)
{
    lv_obj_set_style_border_color(speedometer_offline_border,
                                  lv_color_hex(offline_rgb & 0x00FFFFFFU),
                                  LV_PART_MAIN);
    lv_obj_set_hidden(speedometer_offline_border, !offline);
}
