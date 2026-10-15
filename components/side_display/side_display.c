#include "side_display.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_io_expander_tca9554.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_st7701.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#define SIDE_LCD_H_RES 480
#define SIDE_LCD_V_RES 480
#define SIDE_LCD_PIXEL_CLOCK_HZ (18 * 1000 * 1000)

#define SIDE_I2C_SDA_GPIO 15
#define SIDE_I2C_SCL_GPIO 7
#define SIDE_LCD_SPI_SDA_GPIO 1
#define SIDE_LCD_SPI_SCL_GPIO 2
#define SIDE_LCD_BACKLIGHT_GPIO 6

#define SIDE_LCD_HSYNC_GPIO 38
#define SIDE_LCD_VSYNC_GPIO 39
#define SIDE_LCD_DE_GPIO 40
#define SIDE_LCD_PCLK_GPIO 41

#define SIDE_LCD_RESET_PIN IO_EXPANDER_PIN_NUM_0
#define SIDE_LCD_CS_PIN IO_EXPANDER_PIN_NUM_2

static const char *TAG = "side_display";
static lv_obj_t *link_arc;
static lv_obj_t *link_status;
static lv_obj_t *sequence_label;
static lv_obj_t *uptime_label;

static const st7701_lcd_init_cmd_t lcd_init_commands[] = {
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t[]){0x08}, 1, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]){0x3B, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x10, 0x0C}, 2, 0},
    {0xC2, (uint8_t[]){0x07, 0x0A}, 2, 0},
    {0xC7, (uint8_t[]){0x00}, 1, 0},
    {0xCC, (uint8_t[]){0x10}, 1, 0},
    {0xCD, (uint8_t[]){0x08}, 1, 0},
    {0xB0, (uint8_t[]){0x05, 0x12, 0x98, 0x0E, 0x0F, 0x07, 0x07, 0x09,
                       0x09, 0x23, 0x05, 0x52, 0x0F, 0x67, 0x2C, 0x11}, 16, 0},
    {0xB1, (uint8_t[]){0x0B, 0x11, 0x97, 0x0C, 0x12, 0x06, 0x06, 0x08,
                       0x08, 0x22, 0x03, 0x51, 0x11, 0x66, 0x2B, 0x0F}, 16, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]){0x5D}, 1, 0},
    {0xB1, (uint8_t[]){0x3E}, 1, 0},
    {0xB2, (uint8_t[]){0x81}, 1, 0},
    {0xB3, (uint8_t[]){0x80}, 1, 0},
    {0xB5, (uint8_t[]){0x4E}, 1, 0},
    {0xB7, (uint8_t[]){0x85}, 1, 0},
    {0xB8, (uint8_t[]){0x20}, 1, 0},
    {0xC1, (uint8_t[]){0x78}, 1, 0},
    {0xC2, (uint8_t[]){0x78}, 1, 0},
    {0xD0, (uint8_t[]){0x88}, 1, 0},
    {0xE0, (uint8_t[]){0x00, 0x00, 0x02}, 3, 0},
    {0xE1, (uint8_t[]){0x06, 0x30, 0x08, 0x30, 0x05, 0x30, 0x07, 0x30,
                       0x00, 0x33, 0x33}, 11, 0},
    {0xE2, (uint8_t[]){0x11, 0x11, 0x33, 0x33, 0xF4, 0x00, 0x00, 0x00,
                       0xF4, 0x00, 0x00, 0x00}, 12, 0},
    {0xE3, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE4, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE5, (uint8_t[]){0x0D, 0xF5, 0x30, 0xF0, 0x0F, 0xF7, 0x30, 0xF0,
                       0x09, 0xF1, 0x30, 0xF0, 0x0B, 0xF3, 0x30, 0xF0}, 16, 0},
    {0xE6, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE7, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE8, (uint8_t[]){0x0C, 0xF4, 0x30, 0xF0, 0x0E, 0xF6, 0x30, 0xF0,
                       0x08, 0xF0, 0x30, 0xF0, 0x0A, 0xF2, 0x30, 0xF0}, 16, 0},
    {0xE9, (uint8_t[]){0x36, 0x01}, 2, 0},
    {0xEB, (uint8_t[]){0x00, 0x01, 0xE4, 0xE4, 0x44, 0x88, 0x40}, 7, 0},
    {0xED, (uint8_t[]){0xFF, 0x10, 0xAF, 0x76, 0x54, 0x2B, 0xCF, 0xFF,
                       0xFF, 0xFC, 0xB2, 0x45, 0x67, 0xFA, 0x01, 0xFF}, 16, 0},
    {0xEF, (uint8_t[]){0x08, 0x08, 0x08, 0x45, 0x3F, 0x54}, 6, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x11, NULL, 0, 120},
    {0x3A, (uint8_t[]){0x66}, 1, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x29, NULL, 0, 20},
};

static esp_err_t backlight_start(void)
{
    const ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t channel_config = {
        .gpio_num = SIDE_LCD_BACKLIGHT_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 6143,
        .hpoint = 0,
    };

    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG,
                        "Backlight timer configuration failed");
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG,
                        "Backlight channel configuration failed");
    return ESP_OK;
}

static lv_obj_t *label_create(lv_obj_t *parent, const char *text,
                              const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    return label;
}

static void link_gauge_create(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x070A0D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    link_arc = lv_arc_create(screen);
    lv_obj_set_size(link_arc, 408, 408);
    lv_obj_center(link_arc);
    lv_arc_set_bg_angles(link_arc, 135, 45);
    lv_arc_set_range(link_arc, 0, 59);
    lv_arc_set_value(link_arc, 0);
    lv_obj_remove_style(link_arc, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(link_arc, false);
    lv_obj_set_style_arc_width(link_arc, 28, LV_PART_MAIN);
    lv_obj_set_style_arc_color(link_arc, lv_color_hex(0x253039), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(link_arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(link_arc, 28, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(link_arc, lv_color_hex(0x20C7C9),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(link_arc, true, LV_PART_INDICATOR);

    lv_obj_t *face = lv_obj_create(screen);
    lv_obj_set_size(face, 304, 304);
    lv_obj_center(face);
    lv_obj_set_scrollable(face, false);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(face, lv_color_hex(0x10161B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(face, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(face, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(face, lv_color_hex(0x32404A), LV_PART_MAIN);

    lv_obj_t *title = label_create(
        screen, "RIGHT NODE", &lv_font_montserrat_24, 0xEEF4F6);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    lv_obj_t *caption = label_create(
        face, "CLUSTER LINK", &lv_font_montserrat_24, 0x84939D);
    lv_obj_align(caption, LV_ALIGN_TOP_MID, 0, 55);

    link_status = label_create(
        face, "WAITING", &lv_font_montserrat_48, 0xF6B73C);
    lv_obj_align(link_status, LV_ALIGN_CENTER, 0, -30);

    sequence_label = label_create(
        face, "SEQ --", &lv_font_montserrat_24, 0xD7E1E5);
    lv_obj_align(sequence_label, LV_ALIGN_CENTER, 0, 30);

    uptime_label = label_create(
        face, "CENTER --s", &lv_font_montserrat_24, 0x84939D);
    lv_obj_align(uptime_label, LV_ALIGN_BOTTOM_MID, 0, -54);

    lv_obj_t *footer = label_create(
        screen, "GPIO44  |  460800", &lv_font_montserrat_24, 0x65747E);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -27);
}

esp_err_t side_display_start(void)
{
    static const int data_gpio_nums[16] = {
        5, 45, 48, 47, 21, 14, 13, 12,
        11, 10, 9, 46, 3, 8, 18, 17,
    };
    i2c_master_bus_handle_t i2c_bus = NULL;
    esp_io_expander_handle_t io_expander = NULL;
    esp_lcd_panel_io_handle_t panel_io = NULL;
    esp_lcd_panel_handle_t panel = NULL;

    ESP_LOGI(TAG, "Initializing Waveshare 2.8C 480x480 display");
    const i2c_master_bus_config_t i2c_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = SIDE_I2C_SDA_GPIO,
        .scl_io_num = SIDE_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_config, &i2c_bus), TAG,
                        "I2C bus initialization failed");
    ESP_RETURN_ON_ERROR(esp_io_expander_new_i2c_tca9554(
                            i2c_bus,
                            ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000,
                            &io_expander),
                        TAG, "TCA9554 initialization failed");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(
                            io_expander, SIDE_LCD_RESET_PIN,
                            IO_EXPANDER_OUTPUT),
                        TAG, "LCD reset pin configuration failed");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(
                            io_expander, SIDE_LCD_RESET_PIN, 0),
                        TAG, "LCD reset assertion failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(
                            io_expander, SIDE_LCD_RESET_PIN, 1),
                        TAG, "LCD reset release failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    const spi_line_config_t spi_lines = {
        .cs_io_type = IO_TYPE_EXPANDER,
        .cs_gpio_num = SIDE_LCD_CS_PIN,
        .scl_io_type = IO_TYPE_GPIO,
        .scl_gpio_num = SIDE_LCD_SPI_SCL_GPIO,
        .sda_io_type = IO_TYPE_GPIO,
        .sda_gpio_num = SIDE_LCD_SPI_SDA_GPIO,
        .io_expander = io_expander,
    };
    const esp_lcd_panel_io_3wire_spi_config_t panel_io_config =
        ST7701_PANEL_IO_3WIRE_SPI_CONFIG(spi_lines, 0);
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_3wire_spi(
                            &panel_io_config, &panel_io),
                        TAG, "3-wire SPI initialization failed");

    esp_lcd_rgb_panel_config_t rgb_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 3,
        .bounce_buffer_size_px = 10 * SIDE_LCD_H_RES,
        .psram_trans_align = 64,
        .hsync_gpio_num = SIDE_LCD_HSYNC_GPIO,
        .vsync_gpio_num = SIDE_LCD_VSYNC_GPIO,
        .de_gpio_num = SIDE_LCD_DE_GPIO,
        .pclk_gpio_num = SIDE_LCD_PCLK_GPIO,
        .disp_gpio_num = -1,
        .timings = {
            .pclk_hz = SIDE_LCD_PIXEL_CLOCK_HZ,
            .h_res = SIDE_LCD_H_RES,
            .v_res = SIDE_LCD_V_RES,
            .hsync_pulse_width = 8,
            .hsync_back_porch = 10,
            .hsync_front_porch = 50,
            .vsync_pulse_width = 2,
            .vsync_back_porch = 18,
            .vsync_front_porch = 8,
            .flags.pclk_active_neg = false,
        },
        .flags.fb_in_psram = true,
    };
    for (size_t index = 0; index < 16; ++index) {
        rgb_config.data_gpio_nums[index] = data_gpio_nums[index];
    }

    const st7701_vendor_config_t vendor_config = {
        .rgb_config = &rgb_config,
        .init_cmds = lcd_init_commands,
        .init_cmds_size = sizeof(lcd_init_commands) /
                          sizeof(lcd_init_commands[0]),
        .flags = {
            .mirror_by_cmd = 1,
            .enable_io_multiplex = 0,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor_config,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7701(
                            panel_io, &panel_config, &panel),
                        TAG, "ST7701 panel creation failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG,
                        "RGB panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG,
                        "ST7701 panel initialization failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG,
                        "Display enable failed");

    const esp_lv_adapter_config_t adapter_config =
        ESP_LV_ADAPTER_DEFAULT_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&adapter_config), TAG,
                        "LVGL adapter initialization failed");
    const esp_lv_adapter_display_config_t display_config =
        ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
            panel, panel_io, SIDE_LCD_H_RES, SIDE_LCD_V_RES,
            ESP_LV_ADAPTER_ROTATE_0);
    lv_display_t *display = esp_lv_adapter_register_display(&display_config);
    ESP_RETURN_ON_FALSE(display != NULL, ESP_FAIL, TAG,
                        "LVGL display registration failed");
    lv_display_set_default(display);
    ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), TAG,
                        "LVGL task start failed");
    ESP_RETURN_ON_ERROR(esp_lv_adapter_lock(-1), TAG,
                        "LVGL lock failed during UI creation");
    link_gauge_create();
    esp_lv_adapter_unlock();
    ESP_RETURN_ON_ERROR(backlight_start(), TAG,
                        "Backlight initialization failed");
    ESP_LOGI(TAG, "LVGL right link gauge active");
    return ESP_OK;
}

esp_err_t side_display_update_link(uint16_t sequence, uint32_t center_uptime_ms)
{
    ESP_RETURN_ON_FALSE(link_arc != NULL && link_status != NULL &&
                            sequence_label != NULL && uptime_label != NULL,
                        ESP_ERR_INVALID_STATE, TAG,
                        "Link gauge is not initialized");
    ESP_RETURN_ON_ERROR(esp_lv_adapter_lock(100), TAG,
                        "LVGL lock timed out during link update");
    lv_arc_set_value(link_arc, (center_uptime_ms / 1000U) % 60U);
    lv_label_set_text(link_status, "ONLINE");
    lv_obj_set_style_text_color(link_status, lv_color_hex(0x51D88A),
                                LV_PART_MAIN);
    lv_label_set_text_fmt(sequence_label, "SEQ %u", (unsigned)sequence);
    lv_label_set_text_fmt(uptime_label, "CENTER %" PRIu32 "s",
                          center_uptime_ms / 1000U);
    esp_lv_adapter_unlock();
    return ESP_OK;
}