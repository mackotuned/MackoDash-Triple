#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "dash_fonts.h"
#include "settings_schema.h"
#include "side_pages_internal.h"

extern const uint8_t air_face_rgb565_start[]
    asm("_binary_air_480_rgb565_bin_start");
extern const uint8_t air_face_rgb565_end[]
    asm("_binary_air_480_rgb565_bin_end");
extern const uint8_t graphite_air_face_rgb565_start[]
    asm("_binary_graphite_air_480_rgb565_bin_start");
#define AIRCTL_BIN(name) \
    extern const uint8_t name##_start[] asm("_binary_" #name "_bin_start")
AIRCTL_BIN(control_480_bg);
AIRCTL_BIN(sel_all); AIRCTL_BIN(sel_front); AIRCTL_BIN(sel_rear);
AIRCTL_BIN(sel_fl); AIRCTL_BIN(sel_fr); AIRCTL_BIN(sel_rl); AIRCTL_BIN(sel_rr);
AIRCTL_BIN(pressed_prev); AIRCTL_BIN(pressed_next); AIRCTL_BIN(pressed_up);
AIRCTL_BIN(pressed_down); AIRCTL_BIN(pressed_airout); AIRCTL_BIN(pressed_exit);
AIRCTL_BIN(graphite_control_480_bg);
AIRCTL_BIN(graphite_sel_all); AIRCTL_BIN(graphite_sel_front);
AIRCTL_BIN(graphite_sel_rear); AIRCTL_BIN(graphite_sel_fl);
AIRCTL_BIN(graphite_sel_fr); AIRCTL_BIN(graphite_sel_rl);
AIRCTL_BIN(graphite_sel_rr);
AIRCTL_BIN(graphite_pressed_prev); AIRCTL_BIN(graphite_pressed_next);
AIRCTL_BIN(graphite_pressed_up); AIRCTL_BIN(graphite_pressed_down);
AIRCTL_BIN(graphite_pressed_airout); AIRCTL_BIN(graphite_pressed_exit);

#define RGB565_IMAGE(symbol, width, height) \
    {.header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, \
                .w = (width), .h = (height), .stride = (width) * 2U}, \
     .data_size = (width) * (height) * 2U, .data = symbol##_start}

#define CORNER_COUNT 4U
#define PRESSED_IMAGE_COUNT 6U
#define COMMAND_REPEAT_MS 150U
#define AIR_OUT_HOLD_MS 2000U
#define COLOR_OFFLINE 0xFF4D3DU

/* Indices into the pressed-button highlight images. */
enum {
    PRESSED_PREV,
    PRESSED_NEXT,
    PRESSED_UP,
    PRESSED_DOWN,
    PRESSED_AIR_OUT,
    PRESSED_EXIT,
};

static lv_image_dsc_t air_face_image = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
               .w = 480, .h = 480, .stride = 960},
    .data = air_face_rgb565_start,
};
static lv_image_dsc_t control_background_image = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
               .w = 480, .h = 480, .stride = 960},
    .data_size = 480U * 480U * 2U, .data = control_480_bg_start,
};
static lv_image_dsc_t selector_images[CLUSTER_AIR_TARGET_COUNT] = {
    RGB565_IMAGE(sel_all, 232, 152), RGB565_IMAGE(sel_front, 232, 152),
    RGB565_IMAGE(sel_rear, 232, 152), RGB565_IMAGE(sel_fl, 232, 152),
    RGB565_IMAGE(sel_fr, 232, 152), RGB565_IMAGE(sel_rl, 232, 152),
    RGB565_IMAGE(sel_rr, 232, 152),
};
static lv_image_dsc_t pressed_images[PRESSED_IMAGE_COUNT] = {
    RGB565_IMAGE(pressed_prev, 68, 68), RGB565_IMAGE(pressed_next, 68, 68),
    RGB565_IMAGE(pressed_up, 230, 72), RGB565_IMAGE(pressed_down, 230, 72),
    RGB565_IMAGE(pressed_airout, 134, 60),
    RGB565_IMAGE(pressed_exit, 134, 60),
};
/* [theme][index] image data for the swappable overlay bitmaps. */
static const uint8_t *const selector_data[CLUSTER_THEME_COUNT]
                                         [CLUSTER_AIR_TARGET_COUNT] = {
    {sel_all_start, sel_front_start, sel_rear_start, sel_fl_start,
     sel_fr_start, sel_rl_start, sel_rr_start},
    {graphite_sel_all_start, graphite_sel_front_start,
     graphite_sel_rear_start, graphite_sel_fl_start, graphite_sel_fr_start,
     graphite_sel_rl_start, graphite_sel_rr_start},
};
static const uint8_t *const pressed_data[CLUSTER_THEME_COUNT]
                                        [PRESSED_IMAGE_COUNT] = {
    {pressed_prev_start, pressed_next_start, pressed_up_start,
     pressed_down_start, pressed_airout_start, pressed_exit_start},
    {graphite_pressed_prev_start, graphite_pressed_next_start,
     graphite_pressed_up_start, graphite_pressed_down_start,
     graphite_pressed_airout_start, graphite_pressed_exit_start},
};

static const side_air_backend_t *backend;
static lv_obj_t *air_screen;
static lv_obj_t *air_background;
static lv_obj_t *pressure_labels[SIDE_AIR_PRESSURE_COUNT];
static lv_obj_t *corner_arcs[CORNER_COUNT];
static lv_obj_t *p4_status_label;
static lv_obj_t *air_status_label;
static lv_obj_t *compressor_label;
static lv_obj_t *offline_border;
static lv_obj_t *command_overlay;
static lv_obj_t *control_background;
static lv_obj_t *selector_image;
static lv_obj_t *pressed_image_objects[PRESSED_IMAGE_COUNT];
static lv_timer_t *command_repeat_timer;
static lv_timer_t *air_out_hold_timer;
static size_t shown_target;
static cluster_air_ride_command_code_t active_command = CLUSTER_AIR_RIDE_RELEASE;
static bool command_active;
static bool air_out_holding;

static atomic_bool adjustment_open = ATOMIC_VAR_INIT(false);
static atomic_uint_fast8_t adjustment_target = ATOMIC_VAR_INIT(4U);

static size_t current_target(void)
{
    const size_t target = (size_t)atomic_load_explicit(&adjustment_target,
                                                       memory_order_acquire);
    return target < CLUSTER_AIR_TARGET_COUNT ? target
                                             : CLUSTER_AIR_TARGET_COUNT - 1U;
}

bool side_pages_air_controls_armed(void)
{
    if (!backend || !side_pages_center_online()) return false;
    side_air_state_t state;
    backend->get_state(&state);
    return state.connected;
}

void side_pages_get_air_adjustment(bool *open, uint8_t *target)
{
    *open = atomic_load_explicit(&adjustment_open, memory_order_acquire);
    *target = (uint8_t)atomic_load_explicit(&adjustment_target,
                                            memory_order_acquire);
}

void side_pages_remote_air_adjustment(bool active, uint8_t target)
{
    if (active) {
        atomic_store_explicit(&adjustment_target, target, memory_order_release);
    }
    atomic_store_explicit(&adjustment_open, active, memory_order_release);
}

static void status_label_set(lv_obj_t *label, bool ok, const char *ok_text,
                             const char *bad_text, uint32_t ok_rgb)
{
    const char *text = ok ? ok_text : bad_text;
    if (strcmp(lv_label_get_text(label), text) == 0) return;
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(ok ? ok_rgb : COLOR_OFFLINE),
                                LV_PART_MAIN);
}

/* ---- Adjustment overlay ------------------------------------------------ */

static void stop_command(void)
{
    lv_timer_pause(command_repeat_timer);
    if (command_active) {
        command_active = false;
        backend->command(CLUSTER_AIR_RIDE_RELEASE);
    }
}

static void start_command(cluster_air_ride_command_code_t command)
{
    active_command = command;
    command_active = true;
    backend->command(active_command);
    lv_timer_resume(command_repeat_timer);
    lv_timer_reset(command_repeat_timer);
}

static void command_repeat(lv_timer_t *timer)
{
    (void)timer;
    if (!command_active || !side_pages_air_controls_armed()) {
        stop_command();
        return;
    }
    backend->command(active_command);
}

static void select_target(size_t target)
{
    if (target >= CLUSTER_AIR_TARGET_COUNT) return;
    stop_command();
    shown_target = target;
    lv_image_set_src(selector_image, &selector_images[target]);
    atomic_store_explicit(&adjustment_target, (uint8_t)target,
                          memory_order_release);
}

static void step_target(bool next)
{
    const size_t count = CLUSTER_AIR_TARGET_COUNT;
    const size_t target = current_target();
    select_target(next ? (target + 1U) % count : (target + count - 1U) % count);
}

static void set_pressed(size_t index, bool pressed)
{
    lv_obj_set_hidden(pressed_image_objects[index], !pressed);
}

static bool released(lv_event_code_t code)
{
    return code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST;
}

static void movement_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    const bool up = (bool)(uintptr_t)lv_event_get_user_data(event);
    const size_t image = up ? PRESSED_UP : PRESSED_DOWN;
    if (code == LV_EVENT_PRESSED) {
        if (!side_pages_air_controls_armed()) return;
        set_pressed(image, true);
        start_command(cluster_air_target_command((uint8_t)current_target(), up));
    } else if (released(code)) {
        set_pressed(image, false);
        stop_command();
    }
}

static void target_step_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    const bool next = (bool)(uintptr_t)lv_event_get_user_data(event);
    const size_t image = next ? PRESSED_NEXT : PRESSED_PREV;
    if (code == LV_EVENT_PRESSED) {
        set_pressed(image, true);
    } else if (released(code)) {
        set_pressed(image, false);
    } else if (code == LV_EVENT_CLICKED) {
        step_target(next);
    }
}

/* Air out must be held for two seconds before it starts lowering. */
static void air_out_hold_complete(lv_timer_t *timer)
{
    (void)timer;
    lv_timer_pause(air_out_hold_timer);
    if (!air_out_holding || !side_pages_air_controls_armed()) return;
    start_command(cluster_air_target_command((uint8_t)current_target(), false));
}

static void air_out_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED && side_pages_air_controls_armed()) {
        air_out_holding = true;
        set_pressed(PRESSED_AIR_OUT, true);
        lv_timer_resume(air_out_hold_timer);
        lv_timer_reset(air_out_hold_timer);
    } else if (released(code)) {
        air_out_holding = false;
        lv_timer_pause(air_out_hold_timer);
        set_pressed(PRESSED_AIR_OUT, false);
        stop_command();
    }
}

static void close_overlay_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    air_page_leave();
}

static void command_swipe_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if ((code != LV_EVENT_GESTURE_LEFT && code != LV_EVENT_GESTURE_RIGHT) ||
        air_out_holding) {
        return;
    }
    step_target(code == LV_EVENT_GESTURE_LEFT);
}

static void open_overlay_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    select_target((size_t)(uintptr_t)lv_event_get_user_data(event));
    atomic_store_explicit(&adjustment_open, true, memory_order_release);
    lv_obj_set_hidden(command_overlay, false);
}

static lv_obj_t *touch_zone_create(lv_obj_t *parent, int32_t x, int32_t y,
                                   int32_t width, int32_t height,
                                   lv_event_cb_t callback,
                                   lv_event_code_t filter, void *user_data)
{
    lv_obj_t *zone = lv_obj_create(parent);
    lv_obj_set_pos(zone, x, y);
    lv_obj_set_size(zone, width, height);
    lv_obj_set_style_bg_opa(zone, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(zone, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(zone, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(zone, false);
    lv_obj_add_event_cb(zone, callback, filter, user_data);
    return zone;
}

static void command_overlay_create(lv_obj_t *screen)
{
    static const int16_t pressed_xy[PRESSED_IMAGE_COUNT][2] = {
        {44, 82}, {368, 82}, {125, 203}, {125, 279}, {105, 359}, {241, 359},
    };

    command_overlay = lv_obj_create(screen);
    lv_obj_set_size(command_overlay, 480, 480);
    lv_obj_center(command_overlay);
    lv_obj_set_style_radius(command_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(command_overlay, lv_color_hex(0x090A0B),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(command_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(command_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(command_overlay, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(command_overlay, false);
    /* Swipes on the overlay change the target instead of the page. */
    lv_obj_set_gesture_bubble(command_overlay, false);
    lv_obj_add_event_cb(command_overlay, command_swipe_event,
                        LV_EVENT_GESTURE_LEFT, NULL);
    lv_obj_add_event_cb(command_overlay, command_swipe_event,
                        LV_EVENT_GESTURE_RIGHT, NULL);

    control_background = lv_image_create(command_overlay);
    lv_image_set_src(control_background, &control_background_image);
    lv_obj_set_pos(control_background, 0, 0);
    lv_obj_set_clickable(control_background, false);
    selector_image = lv_image_create(command_overlay);
    lv_image_set_src(selector_image, &selector_images[0]);
    lv_obj_set_pos(selector_image, 124, 44);
    lv_obj_set_clickable(selector_image, false);

    for (size_t index = 0; index < PRESSED_IMAGE_COUNT; ++index) {
        lv_obj_t *image = lv_image_create(command_overlay);
        pressed_image_objects[index] = image;
        lv_image_set_src(image, &pressed_images[index]);
        lv_obj_set_pos(image, pressed_xy[index][0], pressed_xy[index][1]);
        lv_obj_set_clickable(image, false);
        lv_obj_set_hidden(image, true);
    }
    touch_zone_create(command_overlay, 44, 82, 68, 68, target_step_event,
                      LV_EVENT_ALL, (void *)(uintptr_t)false);
    touch_zone_create(command_overlay, 368, 82, 68, 68, target_step_event,
                      LV_EVENT_ALL, (void *)(uintptr_t)true);
    touch_zone_create(command_overlay, 125, 203, 230, 72, movement_event,
                      LV_EVENT_ALL, (void *)(uintptr_t)true);
    touch_zone_create(command_overlay, 125, 279, 230, 72, movement_event,
                      LV_EVENT_ALL, (void *)(uintptr_t)false);
    touch_zone_create(command_overlay, 105, 359, 134, 60, air_out_event,
                      LV_EVENT_ALL, NULL);
    touch_zone_create(command_overlay, 241, 359, 134, 60, close_overlay_event,
                      LV_EVENT_ALL, NULL);
    lv_obj_set_hidden(command_overlay, true);

    command_repeat_timer = lv_timer_create(command_repeat, COMMAND_REPEAT_MS,
                                           NULL);
    lv_timer_pause(command_repeat_timer);
    air_out_hold_timer = lv_timer_create(air_out_hold_complete,
                                         AIR_OUT_HOLD_MS, NULL);
    lv_timer_set_repeat_count(air_out_hold_timer, 1);
    lv_timer_pause(air_out_hold_timer);
}

/* ---- Page ---------------------------------------------------------------- */

static void air_page_update(lv_timer_t *timer)
{
    (void)timer;
    if (lv_screen_active() != air_screen) return;

    side_air_state_t state;
    backend->get_state(&state);
    char text[8];
    for (size_t index = 0; index < SIDE_AIR_PRESSURE_COUNT; ++index) {
        if (state.pressure_valid) {
            snprintf(text, sizeof(text), "%u", (unsigned)state.psi[index]);
        } else {
            snprintf(text, sizeof(text), "--");
        }
        side_pages_label_set_text(pressure_labels[index], text);
    }
    for (size_t index = 0; index < CORNER_COUNT; ++index) {
        lv_arc_set_value(corner_arcs[index],
                         state.pressure_valid ? state.psi[index] : 0U);
    }

    const bool online = side_pages_center_online();
    const bool overlay_open = atomic_load_explicit(&adjustment_open,
                                                   memory_order_acquire);
    /* A remote command can retarget the open overlay; only the selector
     * image follows, so a held local command keeps running. */
    if (overlay_open && current_target() != shown_target) {
        shown_target = current_target();
        lv_image_set_src(selector_image, &selector_images[shown_target]);
    }
    lv_obj_set_hidden(command_overlay, !overlay_open);
    status_label_set(p4_status_label, online, "P4 ONLINE", "P4 OFFLINE",
                     0x8C969AU);
    status_label_set(air_status_label, state.connected, "AIR LINK",
                     "AIR OFFLINE", 0xE85B32U);
    side_pages_label_set_text(compressor_label,
                              state.compressor_valid
                                  ? (state.compressor_on ? "COMP ON"
                                                         : "COMP OFF")
                                  : "COMP --");
    lv_obj_set_hidden(offline_border, online);
    if (command_active && (!online || !state.connected)) stop_command();
}

void air_page_create(lv_obj_t *screen, const side_air_backend_t *air_backend)
{
    static const uint16_t corner_angles[CORNER_COUNT][2] = {
        {192, 256}, {284, 348}, {104, 168}, {12, 76},
    };
    static const lv_arc_mode_t corner_modes[CORNER_COUNT] = {
        LV_ARC_MODE_NORMAL, LV_ARC_MODE_REVERSE,
        LV_ARC_MODE_REVERSE, LV_ARC_MODE_NORMAL,
    };
    static const lv_area_t value_areas[SIDE_AIR_PRESSURE_COUNT] = {
        {91, 120, 200, 171}, {279, 120, 388, 171},
        {91, 308, 200, 359}, {279, 308, 388, 359},
        {140, 198, 339, 273},
    };
    static const lv_area_t hit_areas[SIDE_AIR_PRESSURE_COUNT] = {
        {55, 82, 224, 191}, {255, 82, 424, 191},
        {55, 288, 224, 397}, {255, 288, 424, 397},
        {150, 190, 329, 289},
    };

    backend = air_backend;
    air_screen = screen;
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    air_face_image.data_size = (uint32_t)(air_face_rgb565_end -
                                          air_face_rgb565_start);
    air_background = lv_image_create(screen);
    lv_image_set_src(air_background, &air_face_image);
    lv_obj_center(air_background);
    lv_obj_set_clickable(air_background, false);

    for (size_t index = 0; index < CORNER_COUNT; ++index) {
        lv_obj_t *arc = lv_arc_create(screen);
        corner_arcs[index] = arc;
        lv_obj_set_size(arc, 444, 444);
        lv_obj_center(arc);
        lv_arc_set_bg_angles(arc, corner_angles[index][0],
                             corner_angles[index][1]);
        lv_arc_set_range(arc, 0, 150);
        lv_arc_set_mode(arc, corner_modes[index]);
        lv_arc_set_value(arc, 0);
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
        lv_obj_set_clickable(arc, false);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 12, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, lv_color_hex(0x5EC8F2U),
                                   LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
        lv_obj_set_hidden(arc, true);
    }

    for (size_t index = 0; index < SIDE_AIR_PRESSURE_COUNT; ++index) {
        const lv_area_t *area = &value_areas[index];
        pressure_labels[index] = side_pages_label_create(
            screen, "--",
            index == SIDE_AIR_TANK ? &dash_micro_72 : &dash_micro_35,
            0xF4F1EA);
        lv_obj_set_pos(pressure_labels[index], area->x1, area->y1);
        lv_obj_set_size(pressure_labels[index], lv_area_get_width(area),
                        lv_area_get_height(area));
    }

    p4_status_label = side_pages_label_create(
        screen, "P4 OFFLINE", &lv_font_montserrat_14, COLOR_OFFLINE);
    lv_obj_align(p4_status_label, LV_ALIGN_TOP_MID, 0, 18);
    air_status_label = side_pages_label_create(
        screen, "AIR OFFLINE", &lv_font_montserrat_14, COLOR_OFFLINE);
    lv_obj_align(air_status_label, LV_ALIGN_BOTTOM_MID, 0, -19);
    compressor_label = side_pages_label_create(
        screen, "COMP --", &lv_font_montserrat_14, 0x8C969A);
    lv_obj_align(compressor_label, LV_ALIGN_CENTER, 0, 65);

    for (size_t index = 0; index < SIDE_AIR_PRESSURE_COUNT; ++index) {
        const lv_area_t *area = &hit_areas[index];
        touch_zone_create(screen, area->x1, area->y1, lv_area_get_width(area),
                          lv_area_get_height(area), open_overlay_event,
                          LV_EVENT_CLICKED, (void *)(uintptr_t)index);
    }

    offline_border = lv_obj_create(screen);
    lv_obj_set_size(offline_border, 472, 472);
    lv_obj_center(offline_border);
    lv_obj_set_style_radius(offline_border, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(offline_border, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(offline_border, 6, LV_PART_MAIN);
    lv_obj_set_style_border_color(offline_border, lv_color_hex(COLOR_OFFLINE),
                                  LV_PART_MAIN);
    lv_obj_set_clickable(offline_border, false);

    command_overlay_create(screen);
    lv_timer_create(air_page_update, 100, NULL);
}

void air_page_set_graphite(bool graphite)
{
    const size_t theme = graphite ? CLUSTER_THEME_GRAPHITE
                                  : CLUSTER_THEME_CLASSIC;
    air_face_image.data = graphite ? graphite_air_face_rgb565_start
                                   : air_face_rgb565_start;
    control_background_image.data = graphite ? graphite_control_480_bg_start
                                             : control_480_bg_start;
    for (size_t index = 0; index < CLUSTER_AIR_TARGET_COUNT; ++index) {
        selector_images[index].data = selector_data[theme][index];
    }
    for (size_t index = 0; index < PRESSED_IMAGE_COUNT; ++index) {
        pressed_images[index].data = pressed_data[theme][index];
        lv_image_set_src(pressed_image_objects[index], &pressed_images[index]);
    }
    lv_image_set_src(air_background, &air_face_image);
    lv_image_set_src(control_background, &control_background_image);
    for (size_t index = 0; index < CORNER_COUNT; ++index) {
        lv_obj_set_hidden(corner_arcs[index], !graphite);
    }
    select_target(current_target());
}

void air_page_leave(void)
{
    stop_command();
    atomic_store_explicit(&adjustment_open, false, memory_order_release);
    lv_obj_set_hidden(command_overlay, true);
}
