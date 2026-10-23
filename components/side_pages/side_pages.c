#include "side_pages.h"

#include <stdatomic.h>
#include <string.h>

#include "climate_display.h"
#include "esp_check.h"
#include "settings_schema.h"
#include "side_display.h"
#include "side_pages_internal.h"
#include "side_speedometer.h"
#include "vehicle_data.h"

#define LOCK_TIMEOUT_MS 100

/* Swipe order. The data-driven pages come first. */
typedef enum {
    PAGE_DATA,
    PAGE_GAUGES,
    PAGE_TRENDS,
    PAGE_PEAKS,
    PAGE_TIMER,
    PAGE_SPEEDOMETER,
    PAGE_AIR_RIDE,
    PAGE_CLIMATE,
    PAGE_COUNT,
} side_page_t;

static const side_page_ops_t *const data_pages[] = {
    [PAGE_DATA] = &data_page_ops,
    [PAGE_GAUGES] = &gauges_page_ops,
    [PAGE_TRENDS] = &trends_page_ops,
    [PAGE_PEAKS] = &peaks_page_ops,
    [PAGE_TIMER] = &timer_page_ops,
};
#define DATA_PAGE_COUNT (sizeof(data_pages) / sizeof(data_pages[0]))

static const struct {
    cluster_display_view_t view;
    side_page_t page;
} view_pages[] = {
    {CLUSTER_VIEW_DATA, PAGE_DATA},
    {CLUSTER_VIEW_GAUGES, PAGE_GAUGES},
    {CLUSTER_VIEW_TRENDS, PAGE_TRENDS},
    {CLUSTER_VIEW_PEAKS, PAGE_PEAKS},
    {CLUSTER_VIEW_TIMER, PAGE_TIMER},
    {CLUSTER_VIEW_SPEEDOMETER, PAGE_SPEEDOMETER},
    {CLUSTER_VIEW_AIR_RIDE, PAGE_AIR_RIDE},
    {CLUSTER_VIEW_CLIMATE, PAGE_CLIMATE},
};

static const side_pages_config_t *config;
static lv_obj_t *screens[PAGE_COUNT];
static side_page_t page;
static bool page_transitioning;
static uint32_t offline_rgb = 0x84868DU;
static atomic_bool center_online = ATOMIC_VAR_INIT(false);

bool side_pages_center_online(void)
{
    return atomic_load_explicit(&center_online, memory_order_acquire);
}

lv_obj_t *side_pages_label_create(lv_obj_t *parent, const char *text,
                                  const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    return label;
}

void side_pages_label_set_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

static bool page_for_view(cluster_display_view_t view, side_page_t *result)
{
    for (size_t index = 0; index < sizeof(view_pages) / sizeof(view_pages[0]);
         ++index) {
        if (view_pages[index].view == view) {
            *result = view_pages[index].page;
            return true;
        }
    }
    return false;
}

static void update_offline_locked(void)
{
    const bool offline = !side_pages_center_online();
    for (size_t index = 0; index < DATA_PAGE_COUNT; ++index) {
        data_pages[index]->set_offline(offline);
    }
    side_speedometer_set_offline(offline, offline_rgb);
    climate_display_set_offline(offline);
}

/* Leaving the Air Ride page always releases the valves. */
static void switch_page_locked(side_page_t next)
{
    if (page == PAGE_AIR_RIDE && next != PAGE_AIR_RIDE) air_page_leave();
    page = next;
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
    const bool next = code == LV_EVENT_GESTURE_LEFT;
    switch_page_locked((side_page_t)((page + (next ? 1 : PAGE_COUNT - 1)) %
                                     PAGE_COUNT));
    page_transitioning = true;
    lv_screen_load_anim(screens[page],
                        next ? LV_SCREEN_LOAD_ANIM_MOVE_LEFT
                             : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT,
                        250U, 0U, false);
}

void side_pages_create(lv_obj_t *screen, void *context)
{
    config = context;
    for (size_t index = 0; index < DATA_PAGE_COUNT; ++index) {
        screens[index] = index == PAGE_DATA ? screen : lv_obj_create(NULL);
        data_pages[index]->create(screens[index]);
    }
    screens[PAGE_SPEEDOMETER] = lv_obj_create(NULL);
    side_speedometer_create(screens[PAGE_SPEEDOMETER]);
    screens[PAGE_AIR_RIDE] = lv_obj_create(NULL);
    air_page_create(screens[PAGE_AIR_RIDE], config->air);
    screens[PAGE_CLIMATE] = climate_display_create();

    for (size_t index = 0; index < PAGE_COUNT; ++index) {
        lv_obj_add_event_cb(screens[index], page_swipe_event,
                            LV_EVENT_GESTURE_LEFT, NULL);
        lv_obj_add_event_cb(screens[index], page_swipe_event,
                            LV_EVENT_GESTURE_RIGHT, NULL);
        lv_obj_add_event_cb(screens[index], page_loaded_event,
                            LV_EVENT_SCREEN_LOADED, NULL);
    }
    update_offline_locked();

    side_page_t first = PAGE_DATA;
    page_for_view(config->first_view, &first);
    page = first;
    if (first != PAGE_DATA) lv_screen_load(screens[first]);
}

void side_pages_show_view(cluster_display_view_t view)
{
    side_page_t next;
    if (!page_for_view(view, &next)) return;
    if (side_display_lock(LOCK_TIMEOUT_MS) != ESP_OK) return;
    switch_page_locked(next);
    lv_screen_load(screens[page]);
    side_display_unlock();
}

void side_pages_set_center_online(bool online)
{
    atomic_store_explicit(&center_online, online, memory_order_release);
    if (side_display_lock(LOCK_TIMEOUT_MS) != ESP_OK) return;
    update_offline_locked();
    side_display_unlock();
}

void side_pages_apply_settings(const cluster_left_settings_t *settings)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(side_display_set_brightness(
        config->display == CLUSTER_DISPLAY_RIGHT ? settings->right_brightness
                                                 : settings->brightness));
    if (side_display_lock(LOCK_TIMEOUT_MS) != ESP_OK) return;
    const bool graphite = settings->theme == CLUSTER_THEME_GRAPHITE;
    offline_rgb = settings->offline_rgb;
    for (size_t index = 0; index < DATA_PAGE_COUNT; ++index) {
        data_pages[index]->apply_settings(settings, config->display);
    }
    air_page_set_graphite(graphite);
    side_speedometer_set_graphite(graphite);
    side_speedometer_set_accent(settings->tach_rgb);
    climate_display_set_graphite(graphite);
    update_offline_locked();
    side_display_unlock();
}

void side_pages_update_vehicle(const cluster_vehicle_fast_t *vehicle)
{
    side_speedometer_update(
        vehicle->speed_tenths_kph,
        vehicle_data_channel_valid(vehicle->valid_channels,
                                   VEHICLE_DATA_SPEED));
    if (side_display_lock(LOCK_TIMEOUT_MS) != ESP_OK) return;
    /* Every data page keeps tracking (peaks, timer, trend history) even
     * while another page is on screen. */
    for (size_t index = 0; index < DATA_PAGE_COUNT; ++index) {
        data_pages[index]->update(vehicle);
    }
    side_display_unlock();
}
