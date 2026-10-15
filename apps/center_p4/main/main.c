#include <inttypes.h>

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "cluster_board_config.h"
#include "cluster_protocol.h"
#include "cluster_uart.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "settings_schema.h"
#include "vehicle_data.h"

static const char *TAG = "triple_center";

typedef struct {
    const char *text;
    int16_t x;
    int16_t y;
} dial_tick_t;

static lv_obj_t *dial_label_create(lv_obj_t *parent, const char *text,
                                   const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    return label;
}

static void display_dial_create(void)
{
    static const dial_tick_t ticks[] = {
        {"0", 163, 588}, {"1", 81, 435}, {"2", 104, 264},
        {"3", 209, 127}, {"4", 375, 76}, {"5", 541, 127},
        {"6", 652, 264}, {"7", 669, 435}, {"8", 587, 588},
    };
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x07090C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *rpm_arc = lv_arc_create(screen);
    lv_obj_set_size(rpm_arc, 672, 672);
    lv_obj_center(rpm_arc);
    lv_arc_set_bg_angles(rpm_arc, 135, 45);
    lv_arc_set_range(rpm_arc, 0, 8000);
    lv_arc_set_value(rpm_arc, 5750);
    lv_obj_remove_style(rpm_arc, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(rpm_arc, false);
    lv_obj_set_style_arc_width(rpm_arc, 38, LV_PART_MAIN);
    lv_obj_set_style_arc_color(rpm_arc, lv_color_hex(0x252B31), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(rpm_arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(rpm_arc, 38, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(rpm_arc, lv_color_hex(0xF04438), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(rpm_arc, true, LV_PART_INDICATOR);

    lv_obj_t *dial_face = lv_obj_create(screen);
    lv_obj_set_size(dial_face, 490, 490);
    lv_obj_center(dial_face);
    lv_obj_set_scrollable(dial_face, false);
    lv_obj_set_style_radius(dial_face, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dial_face, lv_color_hex(0x101419), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dial_face, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dial_face, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(dial_face, lv_color_hex(0x303840), LV_PART_MAIN);

    for (size_t index = 0; index < sizeof(ticks) / sizeof(ticks[0]); ++index) {
        lv_obj_t *tick = dial_label_create(
            screen, ticks[index].text, &lv_font_montserrat_24, 0xD5DBE1);
        lv_obj_set_width(tick, 50);
        lv_obj_set_pos(tick, ticks[index].x, ticks[index].y);
    }

    lv_obj_t *brand = dial_label_create(
        screen, "MACKODASH", &lv_font_montserrat_24, 0xF4F6F8);
    lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, 23);

    lv_obj_t *caption = dial_label_create(
        dial_face, "ENGINE SPEED", &lv_font_montserrat_24, 0x8D99A6);
    lv_obj_align(caption, LV_ALIGN_TOP_MID, 0, 82);

    lv_obj_t *value = dial_label_create(
        dial_face, "5,750", &lv_font_montserrat_48, 0xFFFFFF);
    lv_obj_align(value, LV_ALIGN_CENTER, 0, -34);

    lv_obj_t *unit = dial_label_create(
        dial_face, "RPM", &lv_font_montserrat_24, 0xF04438);
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 22);

    lv_obj_t *gear = dial_label_create(
        dial_face, "GEAR  3", &lv_font_montserrat_48, 0xFFFFFF);
    lv_obj_align(gear, LV_ALIGN_BOTTOM_MID, 0, -82);

    lv_obj_t *link = dial_label_create(
        screen, "CENTER P4  |  TX LEFT + RIGHT", &lv_font_montserrat_24,
        0x687582);
    lv_obj_align(link, LV_ALIGN_BOTTOM_MID, 0, -24);
}

static void display_start(void)
{
    bsp_display_cfg_t config = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    ESP_LOGI(TAG, "Starting LVGL on Waveshare 3.4C 800x800 display");
    if (bsp_display_start_with_config(&config) == NULL) {
        ESP_LOGE(TAG, "Failed to start BSP display");
        ESP_ERROR_CHECK(ESP_FAIL);
    }
    ESP_ERROR_CHECK(bsp_display_lock(UINT32_MAX));
    display_dial_create();
    bsp_display_unlock();
    ESP_ERROR_CHECK(bsp_display_backlight_on());
    ESP_LOGI(TAG, "LVGL center dial active");
}

static bool protocol_self_test(void)
{
    const uint8_t payload[] = {0x50, 0x34};
    uint8_t frame[CLUSTER_PROTOCOL_MAX_FRAME_SIZE];
    size_t frame_size = 0;

    if (cluster_frame_encode(CLUSTER_MSG_HEARTBEAT, 1U, 0U, payload,
                             sizeof(payload), frame, sizeof(frame),
                             &frame_size) != CLUSTER_PROTOCOL_OK) {
        return false;
    }

    cluster_frame_view_t view = {0};
    return cluster_frame_decode(frame, frame_size, &view) == CLUSTER_PROTOCOL_OK &&
           view.type == CLUSTER_MSG_HEARTBEAT && view.sequence == 1U &&
           view.payload_size == sizeof(payload);
}

void app_main(void)
{
    ESP_LOGI(TAG, "MackoDash Triple center P4 v0.1.0");
    ESP_LOGI(TAG, "Role: CAN/settings/control authority and center display");
    ESP_LOGI(TAG, "Display links: GPIO%d -> left RXD, GPIO%d -> right RXD at %d baud",
             CLUSTER_CENTER_LEFT_TX_GPIO, CLUSTER_CENTER_RIGHT_TX_GPIO,
             CLUSTER_UART_BAUD_RATE);
    ESP_LOGI(TAG, "Cluster protocol self-test: %s",
             protocol_self_test() ? "PASS" : "FAIL");

    display_start();

    ESP_ERROR_CHECK(cluster_uart_init_tx(
        (uart_port_t)CLUSTER_CENTER_LEFT_UART_PORT,
        CLUSTER_CENTER_LEFT_TX_GPIO, CLUSTER_UART_BAUD_RATE));
    ESP_ERROR_CHECK(cluster_uart_init_tx(
        (uart_port_t)CLUSTER_CENTER_RIGHT_UART_PORT,
        CLUSTER_CENTER_RIGHT_TX_GPIO, CLUSTER_UART_BAUD_RATE));

    uint16_t sequence = 0U;
    uint8_t frame[CLUSTER_PROTOCOL_MAX_FRAME_SIZE];

    for (;;) {
        const uint32_t uptime_ms = (uint32_t)(esp_timer_get_time() / 1000);
        size_t frame_size = 0U;
        ESP_ERROR_CHECK(cluster_frame_encode(
            CLUSTER_MSG_HEARTBEAT, sequence++, uptime_ms, NULL, 0U,
            frame, sizeof(frame), &frame_size));
        ESP_ERROR_CHECK(cluster_uart_send(
            (uart_port_t)CLUSTER_CENTER_LEFT_UART_PORT, frame, frame_size));
        ESP_ERROR_CHECK(cluster_uart_send(
            (uart_port_t)CLUSTER_CENTER_RIGHT_UART_PORT, frame, frame_size));
        ESP_LOGI(TAG, "Heartbeat uptime=%" PRIu32 " ms", uptime_ms);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
