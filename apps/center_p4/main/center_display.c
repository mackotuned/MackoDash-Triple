#include "center_display.h"

#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>

#include "air_overlay.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "center_ui.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lv_decoder.h"
#include "esp_timer.h"
#include "gauge_filter.h"
#include "hondata_can.h"
#include "log_overlay.h"
#include "odometer.h"
#include "settings_service.h"
#include "simulation.h"
#include "vehicle_data.h"

extern const uint8_t tach_face_png_start[]
    asm("_binary_tach_ek_800_png_start");
extern const uint8_t tach_face_png_end[]
    asm("_binary_tach_ek_800_png_end");
extern const uint8_t graphite_tach_face_png_start[]
    asm("_binary_graphite_tach_ek_800_png_start");
extern const uint8_t graphite_tach_face_png_end[]
    asm("_binary_graphite_tach_ek_800_png_end");

#define RPM_GLOW_LAYER_COUNT 3U
#define RPM_SCALE_MAX 9000U
#define RPM_NEEDLE_LENGTH 363
#define REDLINE_FLASH_PERIOD_MS 100U
#define NEEDLE_FRAME_PERIOD_MS 16U
#define STATUS_PERIOD_MS 50U
#define RPM_FILTER_MS 35.0f
#define GRAPHITE_ACCENT_RGB 0x5EC8F2U
#define REDLINE_DIM_RGB 0x210006U
#define VTEC_LED_RGB 0x39FF8CU
#define SHIFT_LED_RGB 0xE4002BU

static const char *TAG = "center_display";

static const lv_coord_t classic_glow_diameters[RPM_GLOW_LAYER_COUNT] = {
    792, 752, 712,
};
static const lv_opa_t classic_glow_opacities[RPM_GLOW_LAYER_COUNT] = {
    230, 120, 40,
};

static lv_image_dsc_t tach_face_images[CLUSTER_THEME_COUNT];
static lv_obj_t *tach_backgrounds[CLUSTER_THEME_COUNT];
static lv_obj_t *rpm_scale;
static lv_obj_t *rpm_glow_layers[RPM_GLOW_LAYER_COUNT];
static lv_obj_t *rpm_needle;
static lv_obj_t *rpm_needle_hub;
static lv_obj_t *odometer_label;
static lv_obj_t *gear_label;
static lv_obj_t *vtec_led;
static lv_obj_t *shift_led;
static esp_lv_decoder_handle_t image_decoder;

/* Owned by the LVGL task: a copy of the settings, refreshed when they change. */
static cluster_settings_t settings;
static gauge_filter_t rpm_filter;

/* What the tach currently shows, so a frame only touches what changed. */
static struct {
    bool valid;
    uint16_t rpm;
    uint32_t rgb;
    bool vtec_on;
    bool shift_on;
    uint8_t shift_brightness;
} tach;

static atomic_bool display_ready = ATOMIC_VAR_INIT(false);
static atomic_bool startup_sweep_started = ATOMIC_VAR_INIT(false);
static atomic_bool startup_sweep_finished = ATOMIC_VAR_INIT(false);
static atomic_bool settings_pending = ATOMIC_VAR_INIT(false);
static atomic_bool log_view_open = ATOMIC_VAR_INIT(false);
static atomic_bool air_view_forced = ATOMIC_VAR_INIT(false);

static uint32_t uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool graphite_theme(void)
{
    return settings.appearance.theme == CLUSTER_THEME_GRAPHITE;
}

/* ---- Tach ---------------------------------------------------------------- */

static uint32_t tach_color_for_rpm(uint16_t rpm)
{
    uint32_t color = graphite_theme() ? GRAPHITE_ACCENT_RGB
                                      : settings.appearance.tach_rgb;
    for (size_t stage = 0; stage < CLUSTER_SHIFT_STAGE_COUNT; ++stage) {
        if (rpm >= settings.engine.shift_stage_rpm[stage]) {
            color = settings.engine.shift_stage_rgb[stage];
        }
    }
    if (rpm >= settings.engine.redline_rpm) {
        color = settings.appearance.critical_rgb;
    }
    return color;
}

static void set_led(lv_obj_t *led, bool active, uint32_t color,
                    uint8_t brightness)
{
    lv_obj_set_style_bg_color(
        led, lv_color_hex(active ? color : 0x171719U), LV_PART_MAIN);
    lv_obj_set_style_border_color(
        led, lv_color_hex(active ? color : 0x4A4A4DU), LV_PART_MAIN);
    lv_obj_set_style_shadow_color(led, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(led, active ? 18 : 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(
        led, active ? (lv_opa_t)((brightness * 255U) / 100U) : LV_OPA_TRANSP,
        LV_PART_MAIN);
}

static void tach_set_rpm(uint16_t rpm)
{
    /* At the redline the arc and shift light blink if flashing is enabled. */
    const bool flash_on = rpm < settings.engine.redline_rpm ||
        !settings.engine.redline_flash ||
        ((uptime_ms() / REDLINE_FLASH_PERIOD_MS) & 1U) == 0U;
    const uint32_t rgb = flash_on ? tach_color_for_rpm(rpm) : REDLINE_DIM_RGB;

    if (!tach.valid || rpm != tach.rpm) {
        for (size_t index = 0; index < RPM_GLOW_LAYER_COUNT; ++index) {
            lv_arc_set_value(rpm_glow_layers[index], rpm);
        }
        lv_scale_set_line_needle_value(rpm_scale, rpm_needle,
                                       RPM_NEEDLE_LENGTH, rpm);
    }
    if (!tach.valid || rgb != tach.rgb) {
        const lv_color_t color = lv_color_hex(rgb);
        for (size_t index = 0; index < RPM_GLOW_LAYER_COUNT; ++index) {
            lv_obj_set_style_arc_color(rpm_glow_layers[index], color,
                                       LV_PART_INDICATOR);
        }
        lv_obj_set_style_line_color(rpm_needle, color, LV_PART_MAIN);
    }

    const bool vtec_on = rpm >= settings.engine.vtec_rpm;
    if (!tach.valid || vtec_on != tach.vtec_on) {
        set_led(vtec_led, vtec_on, VTEC_LED_RGB, 100U);
    }
    const bool shift_on = settings.engine.shift_light_enabled &&
                          rpm >= settings.engine.shift_stage_rpm[0] && flash_on;
    const uint8_t shift_brightness = settings.engine.shift_light_brightness;
    if (!tach.valid || shift_on != tach.shift_on ||
        shift_brightness != tach.shift_brightness) {
        set_led(shift_led, shift_on, SHIFT_LED_RGB, shift_brightness);
    }

    tach.rpm = rpm;
    tach.rgb = rgb;
    tach.vtec_on = vtec_on;
    tach.shift_on = shift_on;
    tach.shift_brightness = shift_brightness;
    tach.valid = true;
}

static void apply_settings_locked(void)
{
    atomic_store_explicit(&settings_pending, false, memory_order_release);
    settings_service_get(&settings);
    const bool graphite = graphite_theme();
    const size_t theme = graphite ? CLUSTER_THEME_GRAPHITE
                                  : CLUSTER_THEME_CLASSIC;

    for (size_t index = 0; index < CLUSTER_THEME_COUNT; ++index) {
        center_ui_theme_image_set_active(tach_backgrounds[index],
                                         index == theme);
    }
    air_overlay_set_theme(theme);
    lv_obj_set_hidden(rpm_needle, graphite);
    lv_obj_set_hidden(rpm_needle_hub, graphite);
    for (size_t index = 0; index < RPM_GLOW_LAYER_COUNT; ++index) {
        lv_obj_t *glow = rpm_glow_layers[index];
        const lv_coord_t diameter = graphite ? 702
                                             : classic_glow_diameters[index];
        lv_obj_set_hidden(glow, graphite && index != 0U);
        lv_obj_set_size(glow, diameter, diameter);
        lv_arc_set_bg_angles(glow, graphite ? 135 : 140, graphite ? 45 : 40);
        lv_obj_set_style_arc_width(glow, graphite ? 26 : 10, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(glow,
                                 graphite ? LV_OPA_COVER
                                          : classic_glow_opacities[index],
                                 LV_PART_INDICATOR);
    }

    tach.valid = false;
    tach_set_rpm(tach.rpm);
    bsp_display_brightness_set(
        (int)settings.appearance.brightness[CLUSTER_SCREEN_CENTER]);
}

/* ---- Timers -------------------------------------------------------------- */

static void needle_frame(lv_timer_t *timer)
{
    (void)timer;
    static bool simulation_was_enabled;
    static uint32_t simulation_started_ms;

    if (!atomic_load_explicit(&display_ready, memory_order_acquire)) return;

    const uint32_t now_ms = uptime_ms();
    const bool simulation_enabled = settings_service_simulation_enabled();
    if (simulation_enabled != simulation_was_enabled) {
        simulation_started_ms = now_ms;
        ESP_LOGI(TAG, "Display simulation %s",
                 simulation_enabled ? "started" : "stopped");
        simulation_was_enabled = simulation_enabled;
    }

    uint16_t target_rpm;
    if (simulation_enabled) {
        target_rpm = simulation_rpm(now_ms - simulation_started_ms);
    } else {
        vehicle_fast_state_t vehicle;
        hondata_can_diagnostics_t diagnostics;
        hondata_can_get_vehicle_fast(&vehicle);
        hondata_can_get_diagnostics(&diagnostics);
        const bool rpm_valid = diagnostics.live_data &&
            vehicle_data_channel_valid(vehicle.valid_channels,
                                       VEHICLE_DATA_RPM);
        target_rpm = !rpm_valid ? 0U
            : (vehicle.rpm <= RPM_SCALE_MAX ? vehicle.rpm : RPM_SCALE_MAX);
    }
    const float display_rpm = gauge_filter_update(
        &rpm_filter, (float)target_rpm, now_ms, RPM_FILTER_MS);
    tach_set_rpm((uint16_t)lroundf(display_rpm));
}

static const char *gear_text(const hondata_state_t *hondata,
                             const vehicle_fast_state_t *vehicle,
                             bool live_data, char *digit, size_t digit_size)
{
    if (settings_service_simulation_enabled()) {
        return vehicle->speed_tenths_kph == 0U ? "N" : "3";
    }
    const bool speed_valid = live_data &&
        vehicle_data_channel_valid(vehicle->valid_channels, VEHICLE_DATA_SPEED);
    if (speed_valid && vehicle->speed_tenths_kph == 0U) return "N";
    const bool gear_valid = live_data &&
        (hondata->received_frame_mask & 1UL) != 0U && hondata->gear < 10U;
    if (!gear_valid) return "--";
    if (hondata->gear == 0U) return "N";
    snprintf(digit, digit_size, "%u", (unsigned)hondata->gear);
    return digit;
}

static void status_update(lv_timer_t *timer)
{
    (void)timer;
    if (atomic_load_explicit(&settings_pending, memory_order_acquire)) {
        apply_settings_locked();
    }
    if (!atomic_load_explicit(&display_ready, memory_order_acquire)) return;

    vehicle_fast_state_t vehicle;
    hondata_state_t hondata;
    hondata_can_diagnostics_t diagnostics;
    hondata_can_get_vehicle_fast(&vehicle);
    hondata_can_get_state(&hondata);
    hondata_can_get_diagnostics(&diagnostics);

    air_overlay_update(
        atomic_load_explicit(&air_view_forced, memory_order_acquire));
    log_overlay_update(
        atomic_load_explicit(&log_view_open, memory_order_acquire), &hondata);

    char text[24];
    const uint64_t odometer_tenths_miles =
        (odometer_get_meters() * 10000ULL) / 1609344ULL;
    snprintf(text, sizeof(text), "%06" PRIu64 ".%" PRIu64,
             odometer_tenths_miles / 10ULL, odometer_tenths_miles % 10ULL);
    center_ui_label_set_text(odometer_label, text);

    char digit[4];
    center_ui_label_set_text(
        gear_label, gear_text(&hondata, &vehicle, diagnostics.live_data,
                              digit, sizeof(digit)));
}

/* ---- Startup sweep ------------------------------------------------------- */

static void startup_sweep_value(void *target, int32_t rpm)
{
    (void)target;
    tach_set_rpm((uint16_t)rpm);
}

static void startup_sweep_complete(lv_anim_t *animation)
{
    (void)animation;
    atomic_store_explicit(&startup_sweep_finished, true, memory_order_release);
}

/* The backlight comes on with the first rendered frame, then the needle
 * sweeps to the limiter and back before live data takes over. */
static void refresh_complete(lv_event_t *event)
{
    (void)event;
    if (!atomic_exchange_explicit(&startup_sweep_started, true,
                                  memory_order_acq_rel)) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_backlight_on());
        ESP_LOGI(TAG, "First frame ready; starting tach sweep");
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_exec_cb(&animation, startup_sweep_value);
        lv_anim_set_values(&animation, 0, RPM_SCALE_MAX);
        lv_anim_set_duration(&animation, 900);
        lv_anim_set_reverse_duration(&animation, 700);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
        lv_anim_set_completed_cb(&animation, startup_sweep_complete);
        lv_anim_start(&animation);
    } else if (atomic_load_explicit(&startup_sweep_finished,
                                    memory_order_acquire) &&
               !atomic_exchange_explicit(&display_ready, true,
                                         memory_order_acq_rel)) {
        ESP_LOGI(TAG, "Startup tach sweep complete");
    }
}

/* ---- Construction -------------------------------------------------------- */

static lv_obj_t *led_create(lv_obj_t *screen, int32_t x_offset)
{
    lv_obj_t *led = lv_obj_create(screen);
    lv_obj_set_size(led, 24, 24);
    lv_obj_align(led, LV_ALIGN_CENTER, x_offset, 342);
    lv_obj_set_style_radius(led, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(led, 2, LV_PART_MAIN);
    lv_obj_set_scrollable(led, false);
    return led;
}

static void dial_create(lv_obj_t *screen)
{
    static const uint8_t *const face_starts[CLUSTER_THEME_COUNT] = {
        tach_face_png_start, graphite_tach_face_png_start,
    };
    static const uint8_t *const face_ends[CLUSTER_THEME_COUNT] = {
        tach_face_png_end, graphite_tach_face_png_end,
    };

    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    for (size_t theme = 0; theme < CLUSTER_THEME_COUNT; ++theme) {
        center_ui_png_init(&tach_face_images[theme], face_starts[theme],
                           face_ends[theme], 800, 800);
        center_ui_flatten_to_rgb565(&tach_face_images[theme]);
        tach_backgrounds[theme] = lv_image_create(screen);
        lv_image_set_src(tach_backgrounds[theme], &tach_face_images[theme]);
        lv_obj_center(tach_backgrounds[theme]);
        lv_obj_set_clickable(tach_backgrounds[theme], false);
        center_ui_theme_image_set_active(tach_backgrounds[theme], false);
    }

    for (size_t index = 0; index < RPM_GLOW_LAYER_COUNT; ++index) {
        lv_obj_t *glow = lv_arc_create(screen);
        rpm_glow_layers[index] = glow;
        lv_obj_set_size(glow, classic_glow_diameters[index],
                        classic_glow_diameters[index]);
        lv_obj_center(glow);
        lv_arc_set_bg_angles(glow, 140, 40);
        lv_arc_set_range(glow, 0, RPM_SCALE_MAX);
        lv_arc_set_value(glow, 0);
        lv_obj_remove_style(glow, NULL, LV_PART_KNOB);
        lv_obj_set_clickable(glow, false);
        lv_obj_set_style_arc_opa(glow, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_width(glow, 10, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(glow, classic_glow_opacities[index],
                                 LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(glow, true, LV_PART_INDICATOR);
    }

    /* Needle geometry matches the face: 0 rpm at 140 degrees, 260 degree
     * sweep, tip at 363 px (see assets/LVGL_NOTES.md). */
    rpm_scale = lv_scale_create(screen);
    lv_obj_set_size(rpm_scale, 800, 800);
    lv_obj_center(rpm_scale);
    lv_scale_set_mode(rpm_scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(rpm_scale, 0, RPM_SCALE_MAX);
    lv_scale_set_angle_range(rpm_scale, 260);
    lv_scale_set_rotation(rpm_scale, 140);
    lv_scale_set_total_tick_count(rpm_scale, 0);
    lv_scale_set_label_show(rpm_scale, false);
    lv_obj_set_style_bg_opa(rpm_scale, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(rpm_scale, 0, LV_PART_MAIN);

    rpm_needle = lv_line_create(rpm_scale);
    lv_obj_set_style_line_width(rpm_needle, 7, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(rpm_needle, true, LV_PART_MAIN);
    lv_scale_set_line_needle_value(rpm_scale, rpm_needle, RPM_NEEDLE_LENGTH, 0);

    rpm_needle_hub = lv_obj_create(screen);
    lv_obj_set_size(rpm_needle_hub, 100, 100);
    lv_obj_center(rpm_needle_hub);
    lv_obj_set_style_radius(rpm_needle_hub, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rpm_needle_hub, lv_color_hex(0x0E0E0F),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rpm_needle_hub, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(rpm_needle_hub, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(rpm_needle_hub, false);

    lv_obj_t *gear_caption = center_ui_label_create(
        screen, "GEAR", &lv_font_montserrat_14, 0x9A9A96);
    lv_obj_align(gear_caption, LV_ALIGN_CENTER, 0, 148);
    gear_label = center_ui_label_create(
        screen, "--", &lv_font_montserrat_24, 0xECECE7);
    lv_obj_align(gear_label, LV_ALIGN_CENTER, 0, 174);

    lv_obj_t *odo_caption = center_ui_label_create(
        screen, "ODO", &lv_font_montserrat_14, 0x9A9A96);
    lv_obj_align(odo_caption, LV_ALIGN_CENTER, 0, 253);

    lv_obj_t *odo_window = lv_obj_create(screen);
    lv_obj_set_size(odo_window, 190, 56);
    lv_obj_align(odo_window, LV_ALIGN_CENTER, 0, 292);
    lv_obj_set_scrollable(odo_window, false);
    lv_obj_set_style_radius(odo_window, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(odo_window, lv_color_hex(0x090909), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(odo_window, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(odo_window, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(odo_window, lv_color_hex(0x575754),
                                  LV_PART_MAIN);
    lv_obj_set_style_pad_all(odo_window, 0, LV_PART_MAIN);
    odometer_label = center_ui_label_create(
        odo_window, "002300.0", &lv_font_montserrat_24, 0xECECE3);
    lv_obj_center(odometer_label);

    vtec_led = led_create(screen, -42);
    shift_led = led_create(screen, 42);

    air_overlay_create(screen);
    log_overlay_create(screen);
    apply_settings_locked();
}

void center_display_start(void)
{
    bsp_display_cfg_t config = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
    };

    ESP_LOGI(TAG, "Starting LVGL on Waveshare 3.4C 800x800 display");
    if (bsp_display_start_with_config(&config) == NULL) {
        ESP_LOGE(TAG, "Failed to start BSP display");
        ESP_ERROR_CHECK(ESP_FAIL);
    }
    ESP_ERROR_CHECK(esp_lv_decoder_init(&image_decoder));
    ESP_ERROR_CHECK(bsp_display_lock(UINT32_MAX));
    dial_create(lv_screen_active());
    lv_timer_create(status_update, STATUS_PERIOD_MS, NULL);
    lv_timer_create(needle_frame, NEEDLE_FRAME_PERIOD_MS, NULL);
    lv_display_add_event_cb(lv_display_get_default(), refresh_complete,
                            LV_EVENT_REFR_READY, NULL);
    bsp_display_unlock();
    ESP_LOGI(TAG, "LVGL center dial active");
}

void center_display_settings_changed(const cluster_settings_t *changed,
                                     void *context)
{
    (void)changed;
    (void)context;
    atomic_store_explicit(&settings_pending, true, memory_order_release);
}

void center_display_show_view(cluster_display_view_t view)
{
    atomic_store_explicit(&log_view_open, view == CLUSTER_VIEW_LOGS,
                          memory_order_release);
    atomic_store_explicit(&air_view_forced, view == CLUSTER_VIEW_AIR_RIDE,
                          memory_order_release);
}

void center_display_close_logs(void)
{
    atomic_store_explicit(&log_view_open, false, memory_order_release);
}
