#include <stdio.h>

#include "dash_fonts.h"
#include "settings_schema.h"
#include "side_pages_internal.h"
#include "vehicle_data.h"

#define DISPLAY_PERIOD_MS 33U
#define MAX_RUN_MS 60000U
/* Below this the car counts as stopped; above it as launched. */
#define STOPPED_MPH 0.5f
#define MPH_TO_MPS 0.44704f
#define ROW_COUNT 4U

typedef enum {
    TIMER_WAITING, /* moving or no data: stop the car to arm */
    TIMER_ARMED,
    TIMER_RUNNING,
    TIMER_DONE,
} timer_state_t;

/* Speed splits first, then distance splits with trap speed. */
typedef struct {
    const char *name;
    float target; /* mph, or metres for distance rows */
    bool distance;
} split_t;

static const split_t splits[ROW_COUNT] = {
    {"0-30 MPH", 30.0f, false},
    {"0-60 MPH", 60.0f, false},
    {"1/8 MILE", 201.168f, true},
    {"1/4 MILE", 402.336f, true},
};
#define QUARTER_MILE_ROW 3U

static lv_obj_t *page_screen;
static lv_obj_t *heading;
static lv_obj_t *status_label;
static lv_obj_t *elapsed_label;
static lv_obj_t *speed_label;
static lv_obj_t *hint;
static lv_obj_t *rows[ROW_COUNT];
static lv_obj_t *row_names[ROW_COUNT];
static lv_obj_t *row_values[ROW_COUNT];
static lv_obj_t *offline_ring;
static const side_palette_t *palette;

static timer_state_t state = TIMER_WAITING;
static bool have_previous;
static uint32_t previous_ms;
static float previous_mph;
static uint32_t start_ms;      /* P4 sample clock */
static uint32_t start_tick;    /* local clock, for the running display */
static uint32_t final_ms;
static float distance_m;
static bool split_done[ROW_COUNT];
static uint32_t split_ms[ROW_COUNT];
static float trap_mph[ROW_COUNT];

static void format_seconds(char *text, size_t size, uint32_t elapsed_ms)
{
    const uint32_t hundredths = (elapsed_ms + 5U) / 10U;
    snprintf(text, size, "%lu.%02lu", (unsigned long)(hundredths / 100U),
             (unsigned long)(hundredths % 100U));
}

static void render_status(void)
{
    static const char *const texts[] = {
        [TIMER_WAITING] = "STOP TO ARM",
        [TIMER_ARMED] = "READY - LAUNCH",
        [TIMER_RUNNING] = "RUNNING",
        [TIMER_DONE] = "RUN COMPLETE",
    };
    side_pages_label_set_text(status_label, texts[state]);
    const uint32_t color = state == TIMER_ARMED ? palette->accent
                         : state == TIMER_DONE  ? palette->accent2
                                                : palette->dim;
    lv_obj_set_style_text_color(status_label, lv_color_hex(color),
                                LV_PART_MAIN);
}

static void render_results(void)
{
    char text[24];
    for (size_t row = 0; row < ROW_COUNT; ++row) {
        if (split_done[row] && splits[row].distance) {
            snprintf(text, sizeof(text), "%s @ %u", splits[row].name,
                     (unsigned)(trap_mph[row] + 0.5f));
        } else {
            snprintf(text, sizeof(text), "%s", splits[row].name);
        }
        side_pages_label_set_text(row_names[row], text);
        if (split_done[row]) {
            format_seconds(text, sizeof(text), split_ms[row]);
        } else {
            snprintf(text, sizeof(text), "--");
        }
        side_pages_label_set_text(row_values[row], text);
    }
}

static void set_state(timer_state_t next)
{
    state = next;
    render_status();
}

static void clear_run(void)
{
    distance_m = 0.0f;
    final_ms = 0U;
    for (size_t row = 0; row < ROW_COUNT; ++row) split_done[row] = false;
    side_pages_label_set_text(elapsed_label, "0.00");
    render_results();
}

static void reset_event(lv_event_t *event)
{
    (void)event;
    clear_run();
    have_previous = false;
    set_state(TIMER_WAITING);
}

static void finish(uint32_t elapsed_ms)
{
    final_ms = elapsed_ms;
    char text[16];
    format_seconds(text, sizeof(text), final_ms);
    side_pages_label_set_text(elapsed_label, text);
    set_state(TIMER_DONE);
}

/* Integrates one sample interval and records any splits crossed in it. */
static void advance(uint32_t now_ms, float mph)
{
    const float dt = (float)(now_ms - previous_ms) / 1000.0f;
    const float previous_distance = distance_m;
    distance_m += (previous_mph + mph) * 0.5f * MPH_TO_MPS * dt;
    const uint32_t segment_start = previous_ms - start_ms;

    for (size_t row = 0; row < ROW_COUNT; ++row) {
        if (split_done[row]) continue;
        const float from = splits[row].distance ? previous_distance : previous_mph;
        const float to = splits[row].distance ? distance_m : mph;
        if (from >= splits[row].target || to < splits[row].target) continue;
        const float fraction = (splits[row].target - from) / (to - from);
        split_ms[row] = segment_start + (uint32_t)(fraction * dt * 1000.0f);
        trap_mph[row] = previous_mph + fraction * (mph - previous_mph);
        split_done[row] = true;
    }
    render_results();

    if (split_done[QUARTER_MILE_ROW]) {
        finish(split_ms[QUARTER_MILE_ROW]);
    } else if (mph < STOPPED_MPH || now_ms - start_ms > MAX_RUN_MS) {
        finish(now_ms - start_ms);
    }
}

static void timer_page_update(const cluster_vehicle_fast_t *vehicle)
{
    char text[16];
    if (!vehicle_data_channel_valid(vehicle->valid_channels,
                                    VEHICLE_DATA_SPEED)) {
        side_pages_label_set_text(speed_label, "NO SPEED DATA");
        if (state == TIMER_RUNNING) finish(previous_ms - start_ms);
        if (state == TIMER_ARMED) set_state(TIMER_WAITING);
        have_previous = false;
        return;
    }
    const uint32_t now_ms = vehicle->sample_time_ms;
    const float mph = (float)vehicle->speed_tenths_kph * 0.0621371f;
    snprintf(text, sizeof(text), "%u MPH", (unsigned)(mph + 0.5f));
    side_pages_label_set_text(speed_label, text);
    if (have_previous && now_ms == previous_ms) return;

    switch (state) {
    case TIMER_WAITING:
        if (mph < STOPPED_MPH) set_state(TIMER_ARMED);
        break;
    case TIMER_ARMED:
        if (mph >= STOPPED_MPH && have_previous && previous_mph < STOPPED_MPH) {
            /* The car was still at the previous sample: start there. */
            clear_run();
            start_ms = previous_ms;
            start_tick = lv_tick_get() - (now_ms - previous_ms);
            set_state(TIMER_RUNNING);
            advance(now_ms, mph);
        }
        break;
    case TIMER_RUNNING:
        advance(now_ms, mph);
        break;
    case TIMER_DONE:
        break;
    }
    previous_ms = now_ms;
    previous_mph = mph;
    have_previous = true;
}

static void display_tick(lv_timer_t *timer)
{
    (void)timer;
    if (state != TIMER_RUNNING || lv_screen_active() != page_screen) return;
    char text[16];
    format_seconds(text, sizeof(text), lv_tick_elaps(start_tick));
    side_pages_label_set_text(elapsed_label, text);
}

static void apply_palette(void)
{
    lv_obj_set_style_bg_color(page_screen, lv_color_hex(palette->background),
                              LV_PART_MAIN);
    lv_obj_set_style_text_color(heading, lv_color_hex(palette->accent),
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(elapsed_label, lv_color_hex(palette->text),
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(speed_label, lv_color_hex(palette->dim),
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_color_hex(palette->dim), LV_PART_MAIN);
    for (size_t row = 0; row < ROW_COUNT; ++row) {
        side_panel_style(rows[row], palette);
        lv_obj_set_style_text_color(row_names[row], lv_color_hex(palette->dim),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_color(row_values[row],
                                    lv_color_hex(palette->text), LV_PART_MAIN);
    }
    render_status();
}

static void timer_page_create(lv_obj_t *screen)
{
    page_screen = screen;
    palette = side_palette(false);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    heading = side_pages_label_create(screen, "PERFORMANCE",
                                      &lv_font_montserrat_14, palette->accent);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 58);
    status_label = side_pages_label_create(screen, "", &lv_font_montserrat_14,
                                           palette->dim);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 80);
    elapsed_label = side_pages_label_create(screen, "0.00", &dash_micro_72,
                                            palette->text);
    lv_obj_align(elapsed_label, LV_ALIGN_TOP_MID, 0, 104);
    speed_label = side_pages_label_create(screen, "-- MPH",
                                          &lv_font_montserrat_14, palette->dim);
    lv_obj_align(speed_label, LV_ALIGN_TOP_MID, 0, 186);

    for (size_t row = 0; row < ROW_COUNT; ++row) {
        rows[row] = side_panel_create(screen, 92, 214 + (int32_t)row * 44, 296,
                                      38);
        row_names[row] = side_pages_label_create(
            rows[row], splits[row].name, &lv_font_montserrat_14, palette->dim);
        lv_obj_align(row_names[row], LV_ALIGN_LEFT_MID, 14, 0);
        row_values[row] = side_pages_label_create(rows[row], "--",
                                                  &dash_micro_28, palette->text);
        lv_obj_align(row_values[row], LV_ALIGN_RIGHT_MID, -14, 1);
    }

    hint = side_pages_label_create(screen, "HOLD TO RESET",
                                   &lv_font_montserrat_14, palette->dim);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 398);
    lv_obj_add_event_cb(screen, reset_event, LV_EVENT_LONG_PRESSED, NULL);

    offline_ring = side_offline_ring_create(screen);
    apply_palette();
    clear_run();
    lv_timer_create(display_tick, DISPLAY_PERIOD_MS, NULL);
}

static void timer_page_apply_settings(const cluster_left_settings_t *settings,
                                      cluster_display_target_t display)
{
    (void)display;
    palette = side_palette(settings->theme == CLUSTER_THEME_GRAPHITE);
    apply_palette();
    lv_obj_set_style_border_color(offline_ring,
                                  lv_color_hex(settings->offline_rgb),
                                  LV_PART_MAIN);
}

static void timer_page_set_offline(bool offline)
{
    lv_obj_set_hidden(offline_ring, !offline);
}

const side_page_ops_t timer_page_ops = {
    .create = timer_page_create,
    .apply_settings = timer_page_apply_settings,
    .update = timer_page_update,
    .set_offline = timer_page_set_offline,
};
