#include "air_overlay.h"

#include <stdio.h>

#include "air_link.h"
#include "center_ui.h"
#include "cluster_protocol.h"
#include "settings_schema.h"

#define AIRCTL_PNG(name) \
    extern const uint8_t name##_start[] asm("_binary_" #name "_png_start"); \
    extern const uint8_t name##_end[] asm("_binary_" #name "_png_end")
AIRCTL_PNG(pressure_800_bg);
AIRCTL_PNG(car_none); AIRCTL_PNG(car_all); AIRCTL_PNG(car_front);
AIRCTL_PNG(car_rear); AIRCTL_PNG(car_fl); AIRCTL_PNG(car_fr);
AIRCTL_PNG(car_rl); AIRCTL_PNG(car_rr);
AIRCTL_PNG(comp_off); AIRCTL_PNG(comp_auto); AIRCTL_PNG(comp_run);
AIRCTL_PNG(graphite_pressure_800_bg);
AIRCTL_PNG(graphite_car_none); AIRCTL_PNG(graphite_car_all);
AIRCTL_PNG(graphite_car_front); AIRCTL_PNG(graphite_car_rear);
AIRCTL_PNG(graphite_car_fl); AIRCTL_PNG(graphite_car_fr);
AIRCTL_PNG(graphite_car_rl); AIRCTL_PNG(graphite_car_rr);
AIRCTL_PNG(graphite_comp_off); AIRCTL_PNG(graphite_comp_auto);
AIRCTL_PNG(graphite_comp_run);
#define PNG_SPAN(name) {name##_start, name##_end}

#define TANK_SEGMENT_COUNT 13U
#define TANK_FULL_PSI 200U
#define CAR_IMAGE_COUNT 8U /* "none" plus one per air target */
#define COMPRESSOR_IMAGE_COUNT 3U

typedef struct {
    const uint8_t *start;
    const uint8_t *end;
} png_span_t;

static const png_span_t background_pngs[CLUSTER_THEME_COUNT] = {
    PNG_SPAN(pressure_800_bg), PNG_SPAN(graphite_pressure_800_bg),
};
static const png_span_t car_pngs[CLUSTER_THEME_COUNT][CAR_IMAGE_COUNT] = {
    {PNG_SPAN(car_none), PNG_SPAN(car_all), PNG_SPAN(car_front),
     PNG_SPAN(car_rear), PNG_SPAN(car_fl), PNG_SPAN(car_fr), PNG_SPAN(car_rl),
     PNG_SPAN(car_rr)},
    {PNG_SPAN(graphite_car_none), PNG_SPAN(graphite_car_all),
     PNG_SPAN(graphite_car_front), PNG_SPAN(graphite_car_rear),
     PNG_SPAN(graphite_car_fl), PNG_SPAN(graphite_car_fr),
     PNG_SPAN(graphite_car_rl), PNG_SPAN(graphite_car_rr)},
};
static const png_span_t compressor_pngs[CLUSTER_THEME_COUNT]
                                       [COMPRESSOR_IMAGE_COUNT] = {
    {PNG_SPAN(comp_off), PNG_SPAN(comp_auto), PNG_SPAN(comp_run)},
    {PNG_SPAN(graphite_comp_off), PNG_SPAN(graphite_comp_auto),
     PNG_SPAN(graphite_comp_run)},
};

static lv_image_dsc_t background_images[CLUSTER_THEME_COUNT];
static lv_image_dsc_t car_images[CLUSTER_THEME_COUNT][CAR_IMAGE_COUNT];
static lv_image_dsc_t compressor_images[CLUSTER_THEME_COUNT]
                                       [COMPRESSOR_IMAGE_COUNT];

static lv_obj_t *overlay;
static lv_obj_t *backgrounds[CLUSTER_THEME_COUNT];
static lv_obj_t *car_objects[CLUSTER_THEME_COUNT];
static lv_obj_t *compressor_objects[CLUSTER_THEME_COUNT];
static lv_obj_t *tank_segments[TANK_SEGMENT_COUNT];
static lv_obj_t *pressure_labels[AIR_LINK_PRESSURE_COUNT];
static lv_obj_t *link_label;
static lv_obj_t *compressor_button;
static size_t active_theme = CLUSTER_THEME_CLASSIC;

/* What is on screen, so updates only touch objects that change. */
static struct {
    bool valid;
    size_t theme;
    size_t car_image;
    size_t compressor_image;
    size_t lit_segments;
    uint8_t highlighted_corners;
    bool connected;
} shown;

static void compressor_button_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    air_link_state_t air;
    air_link_get(&air);
    if (!air.connected) return;
    const bool currently_on = air.compressor_valid && air.compressor_on;
    ESP_ERROR_CHECK_WITHOUT_ABORT(air_link_send_command(
        currently_on ? CLUSTER_AIR_RIDE_COMPRESSOR_OFF
                     : CLUSTER_AIR_RIDE_COMPRESSOR_ON));
}

static lv_obj_t *theme_image_create(const lv_image_dsc_t *source, int32_t x,
                                    int32_t y)
{
    lv_obj_t *image = lv_image_create(overlay);
    lv_image_set_src(image, source);
    lv_obj_set_pos(image, x, y);
    lv_obj_set_clickable(image, false);
    center_ui_theme_image_set_active(image, false);
    return image;
}

void air_overlay_create(lv_obj_t *screen)
{
    static const lv_area_t pressure_areas[AIR_LINK_PRESSURE_COUNT] = {
        [AIR_LINK_FRONT_LEFT] = {88, 300, 277, 385},
        [AIR_LINK_FRONT_RIGHT] = {522, 300, 711, 385},
        [AIR_LINK_REAR_LEFT] = {88, 488, 277, 573},
        [AIR_LINK_REAR_RIGHT] = {522, 488, 711, 573},
        [AIR_LINK_TANK] = {290, 120, 509, 207},
    };
    /* Tank gauge segments, degrees clockwise from 3 o'clock. */
    static const uint16_t tank_angles[TANK_SEGMENT_COUNT][2] = {
        {218, 224}, {226, 232}, {234, 240}, {242, 248}, {250, 256},
        {258, 264}, {266, 273}, {275, 281}, {283, 289}, {291, 297},
        {299, 305}, {307, 313}, {315, 322},
    };

    overlay = center_ui_panel_create(screen, 0x090A0B);

    for (size_t theme = 0; theme < CLUSTER_THEME_COUNT; ++theme) {
        center_ui_png_init(&background_images[theme],
                           background_pngs[theme].start,
                           background_pngs[theme].end, 800, 800);
        center_ui_flatten_to_rgb565(&background_images[theme]);
        backgrounds[theme] = theme_image_create(&background_images[theme], 0, 0);
    }

    for (size_t index = 0; index < TANK_SEGMENT_COUNT; ++index) {
        lv_obj_t *segment = lv_arc_create(overlay);
        tank_segments[index] = segment;
        lv_obj_set_size(segment, 766, 766);
        lv_obj_center(segment);
        lv_arc_set_bg_angles(segment, tank_angles[index][0],
                             tank_angles[index][1]);
        lv_obj_remove_style(segment, NULL, LV_PART_KNOB);
        lv_obj_remove_style(segment, NULL, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(segment, 21, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(segment, false, LV_PART_MAIN);
        lv_obj_set_style_arc_color(segment, lv_color_hex(0xECECE7),
                                   LV_PART_MAIN);
        lv_obj_set_clickable(segment, false);
        lv_obj_set_hidden(segment, true);
    }

    for (size_t theme = 0; theme < CLUSTER_THEME_COUNT; ++theme) {
        for (size_t index = 0; index < CAR_IMAGE_COUNT; ++index) {
            center_ui_png_init(&car_images[theme][index],
                               car_pngs[theme][index].start,
                               car_pngs[theme][index].end, 264, 392);
        }
        car_objects[theme] = theme_image_create(&car_images[theme][0], 268, 226);
        for (size_t index = 0; index < COMPRESSOR_IMAGE_COUNT; ++index) {
            center_ui_png_init(&compressor_images[theme][index],
                               compressor_pngs[theme][index].start,
                               compressor_pngs[theme][index].end, 208, 160);
        }
        compressor_objects[theme] =
            theme_image_create(&compressor_images[theme][0], 296, 626);
    }

    for (size_t index = 0; index < AIR_LINK_PRESSURE_COUNT; ++index) {
        const lv_area_t *area = &pressure_areas[index];
        pressure_labels[index] = center_ui_label_create(
            overlay, "--", &lv_font_montserrat_48, 0xECECE7);
        lv_obj_set_pos(pressure_labels[index], area->x1, area->y1);
        lv_obj_set_size(pressure_labels[index], lv_area_get_width(area),
                        lv_area_get_height(area));
    }
    link_label = center_ui_label_create(overlay, "AIR OFFLINE",
                                        &lv_font_montserrat_14, 0xFF4D3D);
    lv_obj_align(link_label, LV_ALIGN_TOP_MID, 0, 205);

    compressor_button = lv_button_create(overlay);
    lv_obj_set_pos(compressor_button, 340, 628);
    lv_obj_set_size(compressor_button, 120, 120);
    lv_obj_set_style_bg_opa(compressor_button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(compressor_button, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(compressor_button, compressor_button_event,
                        LV_EVENT_CLICKED, NULL);
}

void air_overlay_set_theme(size_t theme)
{
    active_theme = theme;
    for (size_t index = 0; index < CLUSTER_THEME_COUNT; ++index) {
        const bool active = index == theme;
        center_ui_theme_image_set_active(backgrounds[index], active);
        center_ui_theme_image_set_active(car_objects[index], active);
        center_ui_theme_image_set_active(compressor_objects[index], active);
    }
    lv_image_set_src(car_objects[theme], &car_images[theme][0]);
    lv_image_set_src(compressor_objects[theme], &compressor_images[theme][0]);
    shown.valid = false;
}

void air_overlay_update(bool forced_open)
{
    /* Corners each knob target moves: FL, FR, RL, RR bits. */
    static const uint8_t corner_masks[CLUSTER_AIR_TARGET_COUNT] = {
        0x0f, 0x03, 0x0c, 0x01, 0x02, 0x04, 0x08,
    };

    air_link_state_t air;
    air_link_get(&air);
    const bool open = forced_open || (air.fresh && air.adjustment_open);
    lv_obj_set_hidden(overlay, !open);
    if (!open) return;

    const size_t theme = active_theme;
    const bool redraw_all = !shown.valid || shown.theme != theme;
    const bool graphite = theme == CLUSTER_THEME_GRAPHITE;
    const uint32_t accent_rgb = graphite ? 0x5EC8F2U : 0xE81E1EU;
    const uint32_t text_rgb = graphite ? 0xEEF0F2U : 0xECECE7U;
    const uint8_t target = air.adjustment_target < CLUSTER_AIR_TARGET_COUNT
        ? air.adjustment_target : CLUSTER_AIR_TARGET_ALL;

    const size_t car_image = target + 1U;
    if (redraw_all || shown.car_image != car_image) {
        lv_image_set_src(car_objects[theme], &car_images[theme][car_image]);
        shown.car_image = car_image;
    }

    char text[8];
    for (size_t index = 0; index < AIR_LINK_PRESSURE_COUNT; ++index) {
        if (air.pressure_valid) {
            snprintf(text, sizeof(text), "%u", (unsigned)air.psi[index]);
        } else {
            snprintf(text, sizeof(text), "--");
        }
        center_ui_label_set_text(pressure_labels[index], text);
    }
    if (redraw_all || shown.highlighted_corners != corner_masks[target]) {
        for (size_t corner = 0; corner < 4U; ++corner) {
            const bool highlighted = (corner_masks[target] & (1U << corner)) != 0U;
            lv_obj_set_style_text_color(
                pressure_labels[corner],
                lv_color_hex(highlighted ? accent_rgb : text_rgb), LV_PART_MAIN);
        }
        shown.highlighted_corners = corner_masks[target];
    }

    size_t lit_segments = 0U;
    if (air.pressure_valid) {
        lit_segments = ((uint32_t)air.psi[AIR_LINK_TANK] * TANK_SEGMENT_COUNT +
                        TANK_FULL_PSI / 2U) / TANK_FULL_PSI;
        if (lit_segments > TANK_SEGMENT_COUNT) lit_segments = TANK_SEGMENT_COUNT;
    }
    if (redraw_all || shown.lit_segments != lit_segments) {
        /* The leading lit segment is drawn in the accent colour. */
        for (size_t index = 0; index < TANK_SEGMENT_COUNT; ++index) {
            lv_obj_set_hidden(tank_segments[index], index >= lit_segments);
            lv_obj_set_style_arc_color(
                tank_segments[index],
                lv_color_hex(index + 1U == lit_segments ? accent_rgb
                                                        : text_rgb),
                LV_PART_MAIN);
        }
        shown.lit_segments = lit_segments;
    }

    if (redraw_all || shown.connected != air.connected) {
        lv_label_set_text(link_label,
                          air.connected ? "AIR LINK" : "AIR OFFLINE");
        lv_obj_set_style_text_color(
            link_label, lv_color_hex(air.connected ? 0xE85B32 : 0xFF4D3D),
            LV_PART_MAIN);
        if (air.connected) {
            lv_obj_remove_state(compressor_button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(compressor_button, LV_STATE_DISABLED);
        }
        shown.connected = air.connected;
    }

    const size_t compressor_image =
        !air.compressor_valid ? 0U : (air.compressor_on ? 2U : 1U);
    if (redraw_all || shown.compressor_image != compressor_image) {
        lv_image_set_src(compressor_objects[theme],
                         &compressor_images[theme][compressor_image]);
        shown.compressor_image = compressor_image;
    }
    shown.theme = theme;
    shown.valid = true;
}
