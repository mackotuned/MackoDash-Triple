#include <stdio.h>

#include "dash_fonts.h"
#include "settings_schema.h"
#include "side_pages_internal.h"
#include "vehicle_data.h"

#define RING_SIZE 196
#define RING_CENTER_Y (-52)
#define RPM_MAX 9000
#define TILE_COUNT 4U
#define TILE_WIDTH 150
#define TILE_HEIGHT 54

/* Session records shown in the tiles under the ring. */
static const uint8_t tile_channels[TILE_COUNT] = {
    VEHICLE_DATA_SPEED, VEHICLE_DATA_BOOST,
    VEHICLE_DATA_KNOCK, VEHICLE_DATA_COOLANT_TEMP,
};
static const char *const tile_names[TILE_COUNT] = {
    "SPEED", "BOOST", "KNOCK", "COOLANT",
};
static const lv_point_t tile_origins[TILE_COUNT] = {
    {86, 292}, {244, 292}, {86, 352}, {244, 352},
};

static lv_obj_t *page_screen;
static lv_obj_t *heading;
static lv_obj_t *hint;
static lv_obj_t *peak_ring;
static lv_obj_t *live_ring;
static lv_obj_t *peak_caption;
static lv_obj_t *peak_value;
static lv_obj_t *live_value;
static lv_obj_t *tiles[TILE_COUNT];
static lv_obj_t *tile_labels[TILE_COUNT];
static lv_obj_t *tile_values[TILE_COUNT];
static lv_obj_t *offline_ring;
static const side_palette_t *palette;

static int32_t peak_rpm;
static bool peak_rpm_valid;
static int32_t peaks[TILE_COUNT];
static bool peaks_valid[TILE_COUNT];

/* Formats a peak held in tenths, using the channel's normal precision. */
static void format_peak(char *text, size_t size, uint8_t channel,
                        int32_t tenths)
{
    if (channel == VEHICLE_DATA_BOOST) {
        const int32_t magnitude = tenths < 0 ? -tenths : tenths;
        snprintf(text, size, "%s%ld.%ld", tenths < 0 ? "-" : "",
                 (long)(magnitude / 10), (long)(magnitude % 10));
    } else {
        snprintf(text, size, "%ld", (long)((tenths + 5) / 10));
    }
}

static void render_peaks(void)
{
    char text[16];
    snprintf(text, sizeof(text), "%ld", (long)(peak_rpm_valid ? peak_rpm : 0));
    side_pages_label_set_text(peak_value, peak_rpm_valid ? text : "--");
    lv_arc_set_value(peak_ring, peak_rpm_valid ? peak_rpm : 0);
    for (size_t index = 0; index < TILE_COUNT; ++index) {
        if (peaks_valid[index]) {
            format_peak(text, sizeof(text), tile_channels[index], peaks[index]);
        } else {
            snprintf(text, sizeof(text), "--");
        }
        side_pages_label_set_text(tile_values[index], text);
    }
}

static void reset_event(lv_event_t *event)
{
    (void)event;
    peak_rpm_valid = false;
    for (size_t index = 0; index < TILE_COUNT; ++index) {
        peaks_valid[index] = false;
    }
    render_peaks();
}

static lv_obj_t *ring_create(lv_obj_t *screen, int32_t width)
{
    lv_obj_t *ring = lv_arc_create(screen);
    lv_obj_set_size(ring, RING_SIZE, RING_SIZE);
    lv_obj_align(ring, LV_ALIGN_CENTER, 0, RING_CENTER_Y);
    lv_arc_set_bg_angles(ring, 135, 45);
    lv_arc_set_range(ring, 0, RPM_MAX);
    lv_arc_set_value(ring, 0);
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(ring, false);
    lv_obj_set_style_arc_width(ring, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ring, true, LV_PART_INDICATOR);
    return ring;
}

static void apply_palette(void)
{
    lv_obj_set_style_bg_color(page_screen, lv_color_hex(palette->background),
                              LV_PART_MAIN);
    lv_obj_set_style_text_color(heading, lv_color_hex(palette->accent),
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_color_hex(palette->dim), LV_PART_MAIN);
    lv_obj_set_style_arc_color(peak_ring, lv_color_hex(palette->panel),
                               LV_PART_MAIN);
    lv_obj_set_style_arc_color(peak_ring, lv_color_hex(palette->accent),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(live_ring, lv_color_hex(palette->text),
                               LV_PART_INDICATOR);
    lv_obj_set_style_text_color(peak_caption, lv_color_hex(palette->dim),
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(peak_value, lv_color_hex(palette->text),
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(live_value, lv_color_hex(palette->dim),
                                LV_PART_MAIN);
    for (size_t index = 0; index < TILE_COUNT; ++index) {
        side_panel_style(tiles[index], palette);
        lv_obj_set_style_text_color(tile_labels[index],
                                    lv_color_hex(palette->dim), LV_PART_MAIN);
        lv_obj_set_style_text_color(tile_values[index],
                                    lv_color_hex(palette->text), LV_PART_MAIN);
    }
}

static void peaks_page_create(lv_obj_t *screen)
{
    page_screen = screen;
    palette = side_palette(false);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    heading = side_pages_label_create(screen, "SESSION PEAKS",
                                      &lv_font_montserrat_14, palette->accent);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 58);

    /* The thick ring holds the session peak; the thin one tracks live RPM. */
    peak_ring = ring_create(screen, 14);
    live_ring = ring_create(screen, 4);
    lv_obj_set_style_arc_opa(live_ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_size(live_ring, RING_SIZE - 36, RING_SIZE - 36);
    lv_obj_align(live_ring, LV_ALIGN_CENTER, 0, RING_CENTER_Y);

    peak_caption = side_pages_label_create(screen, "PEAK RPM",
                                           &lv_font_montserrat_14, palette->dim);
    lv_obj_align(peak_caption, LV_ALIGN_CENTER, 0, RING_CENTER_Y - 30);
    peak_value = side_pages_label_create(screen, "--", &dash_micro_35,
                                         palette->text);
    lv_obj_align(peak_value, LV_ALIGN_CENTER, 0, RING_CENTER_Y + 4);
    live_value = side_pages_label_create(screen, "NOW --",
                                         &lv_font_montserrat_14, palette->dim);
    lv_obj_align(live_value, LV_ALIGN_CENTER, 0, RING_CENTER_Y + 36);

    for (size_t index = 0; index < TILE_COUNT; ++index) {
        lv_obj_t *tile = side_panel_create(screen, tile_origins[index].x,
                                           tile_origins[index].y, TILE_WIDTH,
                                           TILE_HEIGHT);
        tiles[index] = tile;
        tile_labels[index] = side_pages_label_create(
            tile, tile_names[index], &lv_font_montserrat_14, palette->dim);
        lv_obj_align(tile_labels[index], LV_ALIGN_TOP_LEFT, 12, 5);
        tile_values[index] = side_pages_label_create(tile, "--", &dash_micro_28,
                                                     palette->text);
        lv_obj_align(tile_values[index], LV_ALIGN_BOTTOM_RIGHT, -12, -3);
    }

    hint = side_pages_label_create(screen, "HOLD TO RESET",
                                   &lv_font_montserrat_14, palette->dim);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 414);
    lv_obj_add_event_cb(screen, reset_event, LV_EVENT_LONG_PRESSED, NULL);

    offline_ring = side_offline_ring_create(screen);
    apply_palette();
    render_peaks();
}

static void peaks_page_apply_settings(const cluster_left_settings_t *settings,
                                      cluster_display_target_t display)
{
    (void)display;
    palette = side_palette(settings->theme == CLUSTER_THEME_GRAPHITE);
    apply_palette();
    lv_obj_set_style_border_color(offline_ring,
                                  lv_color_hex(settings->offline_rgb),
                                  LV_PART_MAIN);
}

static void peaks_page_update(const cluster_vehicle_fast_t *vehicle)
{
    bool changed = false;
    char text[16];
    if (vehicle_data_channel_valid(vehicle->valid_channels, VEHICLE_DATA_RPM)) {
        const int32_t rpm = vehicle->rpm;
        lv_arc_set_value(live_ring, rpm > RPM_MAX ? RPM_MAX : rpm);
        snprintf(text, sizeof(text), "NOW %ld", (long)rpm);
        side_pages_label_set_text(live_value, text);
        if (!peak_rpm_valid || rpm > peak_rpm) {
            peak_rpm = rpm > RPM_MAX ? RPM_MAX : rpm;
            peak_rpm_valid = true;
            changed = true;
        }
    } else {
        lv_arc_set_value(live_ring, 0);
        side_pages_label_set_text(live_value, "NOW --");
    }
    for (size_t index = 0; index < TILE_COUNT; ++index) {
        int32_t value;
        if (side_channel_value_tenths(vehicle, tile_channels[index], &value) &&
            (!peaks_valid[index] || value > peaks[index])) {
            peaks[index] = value;
            peaks_valid[index] = true;
            changed = true;
        }
    }
    if (changed) render_peaks();
}

static void peaks_page_set_offline(bool offline)
{
    lv_obj_set_hidden(offline_ring, !offline);
}

const side_page_ops_t peaks_page_ops = {
    .create = peaks_page_create,
    .apply_settings = peaks_page_apply_settings,
    .update = peaks_page_update,
    .set_offline = peaks_page_set_offline,
};
