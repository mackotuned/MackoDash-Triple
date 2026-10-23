#include <stdio.h>

#include "dash_fonts.h"
#include "settings_schema.h"
#include "side_pages_internal.h"
#include "vehicle_data.h"

/* Five samples a second for 20 seconds. */
#define TRACE_COUNT CLUSTER_TREND_SLOT_COUNT
#define SAMPLE_PERIOD_MS 200U
#define POINT_COUNT 100U
/* Samples older than this plot as gaps instead of stale values. */
#define STALE_MS 1000U

static const lv_chart_axis_t trace_axes[TRACE_COUNT] = {
    LV_CHART_AXIS_PRIMARY_Y, LV_CHART_AXIS_SECONDARY_Y,
};
static const int32_t readout_centers[TRACE_COUNT] = {-75, 75};

static lv_obj_t *page_screen;
static lv_obj_t *heading;
static lv_obj_t *subheading;
static lv_obj_t *chart;
static lv_chart_series_t *series[TRACE_COUNT];
static lv_obj_t *names[TRACE_COUNT];
static lv_obj_t *values[TRACE_COUNT];
static lv_obj_t *units[TRACE_COUNT];
static lv_obj_t *offline_ring;
static uint8_t channels[TRACE_COUNT] = {0xFF, 0xFF};
static const side_palette_t *palette;
static cluster_vehicle_fast_t latest_vehicle;
static uint32_t latest_vehicle_ms;
static bool latest_vehicle_valid;

static uint32_t trace_color(size_t trace)
{
    return trace == 0U ? palette->accent : palette->accent2;
}

static void sample(lv_timer_t *timer)
{
    (void)timer;
    const bool fresh = latest_vehicle_valid &&
                       lv_tick_elaps(latest_vehicle_ms) < STALE_MS;
    for (size_t trace = 0; trace < TRACE_COUNT; ++trace) {
        int32_t value;
        if (!fresh || !side_channel_value_tenths(&latest_vehicle,
                                                 channels[trace], &value)) {
            value = LV_CHART_POINT_NONE;
        }
        lv_chart_set_next_value(chart, series[trace], value);
    }
}

static void render_readouts(const cluster_vehicle_fast_t *vehicle)
{
    char text[16];
    for (size_t trace = 0; trace < TRACE_COUNT; ++trace) {
        const uint8_t channel = channels[trace];
        if (vehicle_data_channel_valid(vehicle->valid_channels, channel)) {
            side_channel_format(text, sizeof(text), vehicle, channel);
        } else {
            snprintf(text, sizeof(text), "--");
        }
        side_pages_label_set_text(values[trace], text);
        side_pages_label_set_text(units[trace],
                                  side_channel_unit(vehicle, channel));
    }
}

static void apply_palette(void)
{
    lv_obj_set_style_bg_color(page_screen, lv_color_hex(palette->background),
                              LV_PART_MAIN);
    lv_obj_set_style_text_color(heading, lv_color_hex(palette->accent),
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(subheading, lv_color_hex(palette->dim),
                                LV_PART_MAIN);
    side_panel_style(chart, palette);
    lv_obj_set_style_line_color(chart, lv_color_hex(palette->grid),
                                LV_PART_MAIN);
    for (size_t trace = 0; trace < TRACE_COUNT; ++trace) {
        const lv_color_t color = lv_color_hex(trace_color(trace));
        lv_chart_set_series_color(chart, series[trace], color);
        lv_obj_set_style_text_color(names[trace], color, LV_PART_MAIN);
        lv_obj_set_style_text_color(values[trace], lv_color_hex(palette->text),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_color(units[trace], lv_color_hex(palette->dim),
                                    LV_PART_MAIN);
    }
}

static void trends_page_create(lv_obj_t *screen)
{
    page_screen = screen;
    palette = side_palette(false);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    heading = side_pages_label_create(screen, "TRENDS", &lv_font_montserrat_14,
                                      palette->accent);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 58);
    subheading = side_pages_label_create(screen, "LAST 20 SECONDS",
                                         &lv_font_montserrat_14, palette->dim);
    lv_obj_align(subheading, LV_ALIGN_TOP_MID, 0, 76);

    for (size_t trace = 0; trace < TRACE_COUNT; ++trace) {
        names[trace] = side_pages_label_create(screen, "--",
                                               &lv_font_montserrat_14,
                                               palette->accent);
        lv_obj_align(names[trace], LV_ALIGN_TOP_MID, readout_centers[trace],
                     106);
        values[trace] = side_pages_label_create(screen, "--", &dash_micro_35,
                                                palette->text);
        lv_obj_align(values[trace], LV_ALIGN_TOP_MID, readout_centers[trace],
                     124);
        units[trace] = side_pages_label_create(screen, "",
                                               &lv_font_montserrat_14,
                                               palette->dim);
        lv_obj_align(units[trace], LV_ALIGN_TOP_MID, readout_centers[trace],
                     162);
    }

    chart = lv_chart_create(screen);
    lv_obj_set_pos(chart, 70, 190);
    lv_obj_set_size(chart, 340, 196);
    lv_obj_set_style_radius(chart, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(chart, 10, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);
    lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);
    lv_obj_set_clickable(chart, false);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, POINT_COUNT);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(chart, 5, 0);
    for (size_t trace = 0; trace < TRACE_COUNT; ++trace) {
        series[trace] = lv_chart_add_series(chart,
                                            lv_color_hex(trace_color(trace)),
                                            trace_axes[trace]);
        lv_chart_set_all_values(chart, series[trace], LV_CHART_POINT_NONE);
    }

    offline_ring = side_offline_ring_create(screen);
    apply_palette();
    lv_timer_create(sample, SAMPLE_PERIOD_MS, NULL);
}

static void trends_page_apply_settings(const cluster_left_settings_t *settings,
                                       cluster_display_target_t display)
{
    const uint8_t *selected = display == CLUSTER_DISPLAY_RIGHT
        ? settings->right_trend_channels : settings->left_trend_channels;
    for (size_t trace = 0; trace < TRACE_COUNT; ++trace) {
        if (selected[trace] == channels[trace]) continue;
        channels[trace] = selected[trace];
        int32_t minimum;
        int32_t maximum;
        side_channel_range_tenths(channels[trace], &minimum, &maximum);
        lv_chart_set_axis_range(chart, trace_axes[trace], minimum, maximum);
        lv_chart_set_all_values(chart, series[trace], LV_CHART_POINT_NONE);
        side_pages_label_set_text(names[trace],
                                  side_channel_name(channels[trace]));
    }
    palette = side_palette(settings->theme == CLUSTER_THEME_GRAPHITE);
    apply_palette();
    lv_obj_set_style_border_color(offline_ring,
                                  lv_color_hex(settings->offline_rgb),
                                  LV_PART_MAIN);
    if (latest_vehicle_valid) render_readouts(&latest_vehicle);
}

static void trends_page_update(const cluster_vehicle_fast_t *vehicle)
{
    latest_vehicle = *vehicle;
    latest_vehicle_ms = lv_tick_get();
    latest_vehicle_valid = true;
    render_readouts(vehicle);
}

static void trends_page_set_offline(bool offline)
{
    lv_obj_set_hidden(offline_ring, !offline);
}

const side_page_ops_t trends_page_ops = {
    .create = trends_page_create,
    .apply_settings = trends_page_apply_settings,
    .update = trends_page_update,
    .set_offline = trends_page_set_offline,
};
