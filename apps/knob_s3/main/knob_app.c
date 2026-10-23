#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cluster_board_config.h"
#include "cluster_protocol.h"
#include "cluster_wireless.h"
#include "settings_schema.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_interface.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define LCD_WIDTH 240
#define LCD_HEIGHT 240
#define LCD_BYTES (LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t))
#define LCD_HOST SPI2_HOST
#define LCD_SCLK_GPIO 10
#define LCD_MOSI_GPIO 11
#define LCD_DC_GPIO 3
#define LCD_CS_GPIO 9
#define LCD_RST_GPIO 14
#define LCD_BACKLIGHT_GPIO 46
#define TOUCH_SDA_GPIO 6
#define TOUCH_SCL_GPIO 7
#define TOUCH_INT_GPIO 5
#define TOUCH_RST_GPIO 13
#define ENCODER_A_GPIO 45
#define ENCODER_B_GPIO 42
#define ENCODER_BUTTON_GPIO 41
#define BOARD_POWER_GPIO_1 1
#define BOARD_POWER_GPIO_2 2
#define CST816_ADDRESS 0x15
#define LONG_PRESS_US 600000LL
#define IDLE_TIMEOUT_US 20000000LL
#define LIVE_INTERVAL_US 100000LL
#define SWIPE_THRESHOLD 35
#define RGB565(red, green, blue) \
    ((uint16_t)((((red) & 0xf8) << 8) | (((green) & 0xfc) << 3) | ((blue) >> 3)))

/* Every page bitmap exists in both themes: name.bin and graphite_name.bin. */
#define ASSET(name) \
    extern const uint8_t asset_##name##_start[] \
        asm("_binary_" #name "_bin_start"); \
    extern const uint8_t graphite_asset_##name##_start[] \
        asm("_binary_graphite_" #name "_bin_start")
#define THEMED(name) {asset_##name##_start, graphite_asset_##name##_start}

ASSET(main_0_screen); ASSET(main_1_rec); ASSET(main_2_air);
ASSET(main_3_climate); ASSET(main_4_logs); ASSET(main_5_bright);
ASSET(screen_target_0_left); ASSET(screen_target_1_center);
ASSET(screen_target_2_right); ASSET(screen_target_3_back);
ASSET(screen_source_0_tach); ASSET(screen_source_0_speedo);
ASSET(screen_source_1_data); ASSET(screen_source_1_air);
ASSET(screen_source_2_climate); ASSET(screen_source_2_logs);
ASSET(screen_source_3_back);
ASSET(rec_idle); ASSET(rec_active);
ASSET(air_0_all); ASSET(air_1_front); ASSET(air_2_rear); ASSET(air_3_fl);
ASSET(air_4_fr); ASSET(air_5_rl); ASSET(air_6_rr); ASSET(air_7_back);
ASSET(adjust_air_all); ASSET(adjust_air_front); ASSET(adjust_air_rear);
ASSET(adjust_air_fl); ASSET(adjust_air_fr); ASSET(adjust_air_rl);
ASSET(adjust_air_rr);
ASSET(climate_0_fan); ASSET(climate_1_temp); ASSET(climate_2_mode);
ASSET(climate_3_back); ASSET(adjust_fan); ASSET(adjust_temp);
ASSET(mode_0_face); ASSET(mode_1_bilevel); ASSET(mode_2_feet);
ASSET(mode_3_feetdef); ASSET(mode_4_defrost); ASSET(mode_5_back);
ASSET(logs_0_left); ASSET(logs_1_center); ASSET(logs_2_right);
ASSET(logs_3_back); ASSET(adjust_bright);

typedef const uint8_t *themed_asset_t[CLUSTER_THEME_COUNT];

typedef enum {
    PAGE_MAIN,
    PAGE_SCREEN_TARGET,
    PAGE_SCREEN_SOURCE,
    PAGE_RECORD,
    PAGE_AIR,
    PAGE_AIR_ADJUST,
    PAGE_CLIMATE,
    PAGE_FAN_ADJUST,
    PAGE_TEMP_ADJUST,
    PAGE_MODE,
    PAGE_LOGS,
    PAGE_BRIGHT_ADJUST,
} page_t;

typedef struct {
    page_t page;
    int selection;
    int screen_target;
    int adjust_target;
    int value;
    int old_value;
    int fan_value;
    int temp_value;
    int brightness;
    bool logging_available;
    bool recording;
    int64_t recording_started_us;
    bool online;
    int64_t last_heartbeat_us;
    int64_t last_activity_us;
    bool air_valid;
    uint16_t air_psi[4];
    uint16_t air_last[7];
    uint8_t theme;
} knob_state_t;

typedef struct {
    float start;
    float end;
} segment_t;

static const char *TAG = "mackodash_knob";
static const uint16_t COLOR_WHITE = RGB565(0xec, 0xec, 0xe7);
static const uint16_t COLOR_RED = RGB565(0xe8, 0x1e, 0x1e);
static const segment_t RING20[20] = {
    {140.0f,151.1f},{153.1f,164.2f},{166.2f,177.3f},{179.3f,190.4f},
    {192.4f,203.5f},{205.5f,216.6f},{218.6f,229.7f},{231.7f,242.8f},
    {244.8f,255.9f},{257.9f,269.0f},{271.0f,282.1f},{284.1f,295.2f},
    {297.2f,308.3f},{310.3f,321.4f},{323.4f,334.5f},{336.5f,347.6f},
    {349.6f,360.7f},{362.7f,373.8f},{375.8f,386.9f},{388.9f,400.0f},
};
static const themed_asset_t MAIN_ASSETS[6] = {
    THEMED(main_0_screen), THEMED(main_1_rec), THEMED(main_2_air),
    THEMED(main_3_climate), THEMED(main_4_logs), THEMED(main_5_bright),
};
static const themed_asset_t SCREEN_TARGET_ASSETS[4] = {
    THEMED(screen_target_0_left), THEMED(screen_target_1_center),
    THEMED(screen_target_2_right), THEMED(screen_target_3_back),
};
static const themed_asset_t SCREEN_SOURCE_ASSETS[CLUSTER_DISPLAY_COUNT][4] = {
    [CLUSTER_DISPLAY_LEFT] = {
        THEMED(screen_source_0_speedo), THEMED(screen_source_1_data),
        THEMED(screen_source_2_climate), THEMED(screen_source_3_back),
    },
    [CLUSTER_DISPLAY_CENTER] = {
        THEMED(screen_source_0_tach), THEMED(screen_source_1_air),
        THEMED(screen_source_2_logs), THEMED(screen_source_3_back),
    },
    [CLUSTER_DISPLAY_RIGHT] = {
        THEMED(screen_source_0_speedo), THEMED(screen_source_1_air),
        THEMED(screen_source_2_climate), THEMED(screen_source_3_back),
    },
};
static const cluster_display_view_t SCREEN_SOURCE_VIEWS[CLUSTER_DISPLAY_COUNT][3] = {
    [CLUSTER_DISPLAY_LEFT] = {
        CLUSTER_VIEW_SPEEDOMETER, CLUSTER_VIEW_DATA, CLUSTER_VIEW_CLIMATE,
    },
    [CLUSTER_DISPLAY_CENTER] = {
        CLUSTER_VIEW_TACHOMETER, CLUSTER_VIEW_AIR_RIDE, CLUSTER_VIEW_LOGS,
    },
    [CLUSTER_DISPLAY_RIGHT] = {
        CLUSTER_VIEW_SPEEDOMETER, CLUSTER_VIEW_AIR_RIDE, CLUSTER_VIEW_CLIMATE,
    },
};
static const themed_asset_t RECORD_ASSETS[2] = {
    THEMED(rec_idle), THEMED(rec_active),
};
static const themed_asset_t AIR_ASSETS[8] = {
    THEMED(air_0_all), THEMED(air_1_front), THEMED(air_2_rear),
    THEMED(air_3_fl), THEMED(air_4_fr), THEMED(air_5_rl), THEMED(air_6_rr),
    THEMED(air_7_back),
};
static const themed_asset_t AIR_ADJUST_ASSETS[7] = {
    THEMED(adjust_air_all), THEMED(adjust_air_front), THEMED(adjust_air_rear),
    THEMED(adjust_air_fl), THEMED(adjust_air_fr), THEMED(adjust_air_rl),
    THEMED(adjust_air_rr),
};
static const themed_asset_t CLIMATE_ASSETS[4] = {
    THEMED(climate_0_fan), THEMED(climate_1_temp), THEMED(climate_2_mode),
    THEMED(climate_3_back),
};
static const themed_asset_t MODE_ASSETS[6] = {
    THEMED(mode_0_face), THEMED(mode_1_bilevel), THEMED(mode_2_feet),
    THEMED(mode_3_feetdef), THEMED(mode_4_defrost), THEMED(mode_5_back),
};
static const themed_asset_t LOG_ASSETS[4] = {
    THEMED(logs_0_left), THEMED(logs_1_center), THEMED(logs_2_right),
    THEMED(logs_3_back),
};
static const themed_asset_t FAN_ADJUST_ASSET = THEMED(adjust_fan);
static const themed_asset_t TEMP_ADJUST_ASSET = THEMED(adjust_temp);
static const themed_asset_t BRIGHT_ADJUST_ASSET = THEMED(adjust_bright);

static esp_lcd_panel_io_handle_t lcd_io;
static SemaphoreHandle_t lcd_done;
static SemaphoreHandle_t state_mutex;
static QueueHandle_t encoder_queue;
static uint16_t *framebuffer;
static atomic_int_fast64_t last_live_send_us = ATOMIC_VAR_INIT(0);
static atomic_int_fast64_t last_encoder_activity_us = ATOMIC_VAR_INIT(0);
static knob_state_t state = {
    .page = PAGE_MAIN,
    .brightness = 80,
    .temp_value = 7,
};

static uint16_t wire_color(uint16_t color)
{
    return (uint16_t)((color << 8) | (color >> 8));
}

static void pixel(int x, int y, uint16_t color)
{
    if ((unsigned)x < LCD_WIDTH && (unsigned)y < LCD_HEIGHT) {
        framebuffer[y * LCD_WIDTH + x] = wire_color(color);
    }
}

static void fill_rect(int x, int y, int width, int height, uint16_t color)
{
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            pixel(x + column, y + row, color);
        }
    }
}

static uint8_t glyph_row(char character, int row)
{
    static const uint8_t digits[12][7] = {
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14},{14,17,17,15,1,1,14},
        {0,6,6,0,6,6,0},{0,0,0,31,0,0,0},
    };
    if (character >= '0' && character <= '9') return digits[character - '0'][row];
    if (character == ':') return digits[10][row];
    if (character == '-') return digits[11][row];
    if (character == 'F') {
        static const uint8_t rows[7] = {31,16,16,30,16,16,16};
        return rows[row];
    }
    if (character == 'O') {
        static const uint8_t rows[7] = {14,17,17,17,17,17,14};
        return rows[row];
    }
    if (character == 'L') {
        static const uint8_t rows[7] = {16,16,16,16,16,16,31};
        return rows[row];
    }
    if (character == 'I') {
        static const uint8_t rows[7] = {14,4,4,4,4,4,14};
        return rows[row];
    }
    if (character == 'N') {
        static const uint8_t rows[7] = {17,25,21,19,17,17,17};
        return rows[row];
    }
    if (character == 'E') {
        static const uint8_t rows[7] = {31,16,16,30,16,16,31};
        return rows[row];
    }
    return 0;
}

static int text_width(const char *text, int scale)
{
    return *text ? ((int)strlen(text) * 6 - 1) * scale : 0;
}

static void draw_text(int x, int y, const char *text, int scale, uint16_t color)
{
    for (; *text; ++text, x += 6 * scale) {
        for (int row = 0; row < 7; ++row) {
            const uint8_t bits = glyph_row(*text, row);
            for (int column = 0; column < 5; ++column) {
                if ((bits & (1U << (4 - column))) != 0U) {
                    fill_rect(x + column * scale, y + row * scale,
                              scale, scale, color);
                }
            }
        }
    }
}

static void centered_text(int y, const char *text, int scale, uint16_t color)
{
    draw_text((LCD_WIDTH - text_width(text, scale)) / 2, y, text, scale, color);
}

static void draw_segment(const segment_t *segment, uint16_t color)
{
    for (float degree = segment->start; degree <= segment->end; degree += 0.7f) {
        const float radians = degree * (float)M_PI / 180.0f;
        const int x = 120 + (int)lroundf(cosf(radians) * 108.0f);
        const int y = 120 + (int)lroundf(sinf(radians) * 108.0f);
        fill_rect(x - 5, y - 5, 11, 11, color);
    }
}

static void draw_ring(int lit, bool temperature)
{
    if (lit < 0) lit = 0;
    if (lit > 20) lit = 20;
    for (int index = 0; index < lit; ++index) {
        uint16_t color = index == lit - 1 ? COLOR_RED : COLOR_WHITE;
        if (temperature && index >= 10) color = COLOR_RED;
        draw_segment(&RING20[index], color);
    }
}

static void set_backlight(int percent)
{
    const uint32_t duty = (uint32_t)percent * 1023U / 100U;
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_set_duty(LEDC_LOW_SPEED_MODE,
                                                LEDC_CHANNEL_0, duty));
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_update_duty(LEDC_LOW_SPEED_MODE,
                                                   LEDC_CHANNEL_0));
}

static bool snapshot_online(void)
{
    bool online;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    online = state.online;
    xSemaphoreGive(state_mutex);
    return online;
}

static void send_command(cluster_knob_command_code_t code, uint8_t target,
                         int32_t value, uint8_t flags)
{
    if (!snapshot_online()) return;
    cluster_knob_command_t command = {
        .code = code,
        .target = target,
        .flags = flags,
        .value = value,
    };
    uint8_t payload[CLUSTER_KNOB_COMMAND_PAYLOAD_SIZE];
    if (cluster_knob_command_encode(&command, payload, sizeof(payload)) !=
        CLUSTER_PROTOCOL_OK) {
        return;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(cluster_wireless_send_message(
        CLUSTER_WIRELESS_ROLE_CENTER, CLUSTER_MSG_KNOB_COMMAND, payload,
        sizeof(payload)));
}

static void send_live(cluster_knob_command_code_t code, uint8_t target,
                      int32_t value)
{
    const int64_t now = esp_timer_get_time();
    int64_t previous = atomic_load_explicit(&last_live_send_us,
                                            memory_order_relaxed);
    if (now - previous < LIVE_INTERVAL_US ||
        !atomic_compare_exchange_strong_explicit(
            &last_live_send_us, &previous, now,
            memory_order_relaxed, memory_order_relaxed)) return;
    send_command(code, target, value, CLUSTER_KNOB_COMMAND_LIVE);
}

static int page_count(page_t page)
{
    switch (page) {
    case PAGE_MAIN: return 6;
    case PAGE_SCREEN_TARGET: return 4;
    case PAGE_SCREEN_SOURCE: return 4;
    case PAGE_AIR: return 8;
    case PAGE_CLIMATE: return 4;
    case PAGE_MODE: return 6;
    case PAGE_LOGS: return 4;
    default: return 1;
    }
}

static bool is_adjust_page(page_t page)
{
    return page == PAGE_AIR_ADJUST || page == PAGE_FAN_ADJUST ||
           page == PAGE_TEMP_ADJUST || page == PAGE_BRIGHT_ADJUST;
}

static int air_start_value(const knob_state_t *current, int target)
{
    if (!current->air_valid) return current->air_last[target];
    switch (target) {
    case 0: return (current->air_psi[0] + current->air_psi[1] +
                    current->air_psi[2] + current->air_psi[3] + 2) / 4;
    case 1: return (current->air_psi[0] + current->air_psi[1] + 1) / 2;
    case 2: return (current->air_psi[2] + current->air_psi[3] + 1) / 2;
    default: return current->air_psi[target - 3];
    }
}

static void go_main_locked(void)
{
    state.page = PAGE_MAIN;
    state.selection = 0;
}

static void cancel_adjust(void)
{
    cluster_knob_command_code_t code = CLUSTER_KNOB_AIR_SET;
    uint8_t target;
    int old_value;
    page_t parent;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    target = (uint8_t)state.adjust_target;
    old_value = state.old_value;
    if (state.page == PAGE_FAN_ADJUST) {
        code = CLUSTER_KNOB_HVAC_FAN_SET;
        parent = PAGE_CLIMATE;
        state.fan_value = old_value;
    } else if (state.page == PAGE_TEMP_ADJUST) {
        code = CLUSTER_KNOB_HVAC_BLEND_SET;
        parent = PAGE_CLIMATE;
        state.temp_value = old_value;
    } else if (state.page == PAGE_BRIGHT_ADJUST) {
        code = CLUSTER_KNOB_BRIGHTNESS_SET;
        target = 0;
        parent = PAGE_MAIN;
        state.brightness = old_value;
    } else {
        parent = PAGE_AIR;
        state.air_last[target] = (uint16_t)old_value;
    }
    state.value = old_value;
    state.page = parent;
    state.last_activity_us = esp_timer_get_time();
    xSemaphoreGive(state_mutex);
    if (code == CLUSTER_KNOB_BRIGHTNESS_SET) set_backlight(old_value);
    send_command(code, target, old_value, CLUSTER_KNOB_COMMAND_CANCEL);
}

static void go_back(void)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    const page_t page = state.page;
    xSemaphoreGive(state_mutex);
    if (is_adjust_page(page)) {
        cancel_adjust();
        return;
    }
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    if (page == PAGE_SCREEN_SOURCE) state.page = PAGE_SCREEN_TARGET;
    else if (page == PAGE_MODE) state.page = PAGE_CLIMATE;
    else if (page != PAGE_MAIN) go_main_locked();
    state.last_activity_us = esp_timer_get_time();
    xSemaphoreGive(state_mutex);
    if (page == PAGE_AIR) {
        send_command(CLUSTER_KNOB_AIR_VIEW_SET, 0, 0,
                     CLUSTER_KNOB_COMMAND_CONFIRM);
    }
}

static void go_home(void)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    const bool air_open = state.page == PAGE_AIR ||
                          state.page == PAGE_AIR_ADJUST;
    const bool adjusting = is_adjust_page(state.page);
    xSemaphoreGive(state_mutex);
    if (adjusting) cancel_adjust();

    xSemaphoreTake(state_mutex, portMAX_DELAY);
    go_main_locked();
    state.last_activity_us = esp_timer_get_time();
    xSemaphoreGive(state_mutex);
    if (air_open) {
        send_command(CLUSTER_KNOB_AIR_VIEW_SET, 0, 0,
                     CLUSTER_KNOB_COMMAND_CONFIRM);
    }
}

static void rotate_ui(int direction)
{
    cluster_knob_command_code_t live_code = CLUSTER_KNOB_AIR_SET;
    int live_target = 0;
    int live_value = 0;
    bool send = false;
    bool update_backlight = false;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.last_activity_us = esp_timer_get_time();
    if (!is_adjust_page(state.page)) {
        const int count = page_count(state.page);
        state.selection = (state.selection + direction + count) % count;
    } else {
        int step = 1;
        int minimum = 0;
        int maximum = 150;
        live_target = state.adjust_target;
        if (state.page == PAGE_FAN_ADJUST) {
            maximum = CLUSTER_CLIMATE_FAN_MAX;
            live_code = CLUSTER_KNOB_HVAC_FAN_SET;
        } else if (state.page == PAGE_TEMP_ADJUST) {
            maximum = CLUSTER_CLIMATE_TEMP_MAX;
            live_code = CLUSTER_KNOB_HVAC_BLEND_SET;
        } else if (state.page == PAGE_BRIGHT_ADJUST) {
            minimum = 5;
            maximum = 100;
            step = 5;
            live_target = 0;
            live_code = CLUSTER_KNOB_BRIGHTNESS_SET;
            update_backlight = true;
        }
        state.value += direction * step;
        if (state.value < minimum) state.value = minimum;
        if (state.value > maximum) state.value = maximum;
        if (state.page == PAGE_FAN_ADJUST) state.fan_value = state.value;
        if (state.page == PAGE_TEMP_ADJUST) state.temp_value = state.value;
        if (state.page == PAGE_BRIGHT_ADJUST) state.brightness = state.value;
        if (state.page == PAGE_AIR_ADJUST) {
            state.air_last[state.adjust_target] = (uint16_t)state.value;
        }
        live_value = state.value;
        send = true;
    }
    xSemaphoreGive(state_mutex);
    if (update_backlight) set_backlight(live_value);
    if (send) send_live(live_code, (uint8_t)live_target, live_value);
}

static void press_ui(void)
{
    cluster_knob_command_code_t code = CLUSTER_KNOB_SCREEN_SET;
    uint8_t target = 0;
    int value = 0;
    bool send = false;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state.last_activity_us = esp_timer_get_time();
    switch (state.page) {
    case PAGE_MAIN:
        state.page = (page_t[]){PAGE_SCREEN_TARGET, PAGE_RECORD, PAGE_AIR,
            PAGE_CLIMATE, PAGE_LOGS, PAGE_BRIGHT_ADJUST}[state.selection];
        if (state.page == PAGE_RECORD) state.selection = state.recording ? 1 : 0;
        else if (state.page == PAGE_BRIGHT_ADJUST) {
            state.old_value = state.brightness;
            state.value = state.brightness;
            state.adjust_target = 0;
        } else state.selection = 0;
        if (state.page == PAGE_AIR) {
            code = CLUSTER_KNOB_AIR_VIEW_SET;
            value = 1;
            send = true;
        } else if (state.page == PAGE_CLIMATE) {
            code = CLUSTER_KNOB_SCREEN_SET;
            target = CLUSTER_DISPLAY_RIGHT;
            value = CLUSTER_VIEW_CLIMATE;
            send = true;
        }
        break;
    case PAGE_SCREEN_TARGET:
        if (state.selection == 3) go_main_locked();
        else {
            state.screen_target = state.selection;
            state.page = PAGE_SCREEN_SOURCE;
            state.selection = 0;
        }
        break;
    case PAGE_SCREEN_SOURCE:
        if (state.selection == 3) state.page = PAGE_SCREEN_TARGET;
        else {
            code = CLUSTER_KNOB_SCREEN_SET;
            target = (uint8_t)state.screen_target;
            value = SCREEN_SOURCE_VIEWS[state.screen_target][state.selection];
            send = true;
            go_main_locked();
        }
        break;
    case PAGE_RECORD:
        code = CLUSTER_KNOB_RECORD_SET;
        value = state.recording ? 0 : 1;
        send = true;
        break;
    case PAGE_AIR:
        if (state.selection == 7) {
            code = CLUSTER_KNOB_AIR_VIEW_SET;
            value = 0;
            send = true;
            go_main_locked();
        }
        else {
            state.adjust_target = state.selection;
            state.value = air_start_value(&state, state.selection);
            state.old_value = state.value;
            state.page = PAGE_AIR_ADJUST;
        }
        break;
    case PAGE_CLIMATE:
        if (state.selection == 0) {
            state.page = PAGE_FAN_ADJUST;
            state.value = state.old_value = state.fan_value;
            state.adjust_target = 0;
        } else if (state.selection == 1) {
            state.page = PAGE_TEMP_ADJUST;
            state.value = state.old_value = state.temp_value;
            state.adjust_target = 0;
        } else if (state.selection == 2) {
            state.page = PAGE_MODE;
            state.selection = 0;
        } else go_main_locked();
        break;
    case PAGE_MODE:
        if (state.selection == 5) state.page = PAGE_CLIMATE;
        else {
            code = CLUSTER_KNOB_HVAC_MODE_SET;
            value = state.selection;
            send = true;
            state.page = PAGE_CLIMATE;
            state.selection = 2;
        }
        break;
    case PAGE_LOGS:
        if (state.selection == 3) go_main_locked();
        else {
            code = CLUSTER_KNOB_LOGS_OPEN;
            target = (uint8_t)state.selection;
            send = true;
            go_main_locked();
        }
        break;
    case PAGE_AIR_ADJUST:
        code = CLUSTER_KNOB_AIR_SET;
        target = (uint8_t)state.adjust_target;
        value = state.value;
        send = true;
        state.page = PAGE_AIR;
        state.selection = state.adjust_target;
        break;
    case PAGE_FAN_ADJUST:
        code = CLUSTER_KNOB_HVAC_FAN_SET;
        value = state.value;
        send = true;
        state.page = PAGE_CLIMATE;
        state.selection = 0;
        break;
    case PAGE_TEMP_ADJUST:
        code = CLUSTER_KNOB_HVAC_BLEND_SET;
        value = state.value;
        send = true;
        state.page = PAGE_CLIMATE;
        state.selection = 1;
        break;
    case PAGE_BRIGHT_ADJUST:
        code = CLUSTER_KNOB_BRIGHTNESS_SET;
        value = state.value;
        send = true;
        go_main_locked();
        break;
    }
    xSemaphoreGive(state_mutex);
    if (send) send_command(code, target, value, CLUSTER_KNOB_COMMAND_CONFIRM);
}

static const uint8_t *background_for(const knob_state_t *current)
{
    const size_t theme = current->theme == CLUSTER_THEME_GRAPHITE
        ? CLUSTER_THEME_GRAPHITE : CLUSTER_THEME_CLASSIC;
    const int selection = current->selection;
    switch (current->page) {
    case PAGE_SCREEN_TARGET: return SCREEN_TARGET_ASSETS[selection][theme];
    case PAGE_SCREEN_SOURCE:
        return SCREEN_SOURCE_ASSETS[current->screen_target][selection][theme];
    case PAGE_RECORD: return RECORD_ASSETS[current->recording ? 1 : 0][theme];
    case PAGE_AIR: return AIR_ASSETS[selection][theme];
    case PAGE_AIR_ADJUST: return AIR_ADJUST_ASSETS[current->adjust_target][theme];
    case PAGE_CLIMATE: return CLIMATE_ASSETS[selection][theme];
    case PAGE_FAN_ADJUST: return FAN_ADJUST_ASSET[theme];
    case PAGE_TEMP_ADJUST: return TEMP_ADJUST_ASSET[theme];
    case PAGE_MODE: return MODE_ASSETS[selection][theme];
    case PAGE_LOGS: return LOG_ASSETS[selection][theme];
    case PAGE_BRIGHT_ADJUST: return BRIGHT_ADJUST_ASSET[theme];
    case PAGE_MAIN: return MAIN_ASSETS[selection][theme];
    default: return MAIN_ASSETS[0][theme];
    }
}

static void render(void)
{
    knob_state_t current;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    current = state;
    xSemaphoreGive(state_mutex);
    memcpy(framebuffer, background_for(&current), LCD_BYTES);
    if (is_adjust_page(current.page)) {
        char value_text[8];
        if (current.page == PAGE_FAN_ADJUST && current.value == 0) {
            strcpy(value_text, "OFF");
        } else {
            snprintf(value_text, sizeof(value_text), "%d", current.value);
        }
        /* Each page maps its range onto the 20-segment ring. */
        int lit;
        switch (current.page) {
        case PAGE_FAN_ADJUST:
            lit = current.value * 20 / CLUSTER_CLIMATE_FAN_MAX;
            break;
        case PAGE_TEMP_ADJUST:
            lit = (current.value * 20 + CLUSTER_CLIMATE_TEMP_MAX / 2) /
                  CLUSTER_CLIMATE_TEMP_MAX;
            break;
        case PAGE_AIR_ADJUST:
            lit = current.air_valid ? (current.value * 20 + 75) / 150 : 0;
            break;
        default:
            lit = current.value / 5;
            break;
        }
        draw_ring(lit, current.page == PAGE_TEMP_ADJUST);
        centered_text(106, value_text, 5, COLOR_WHITE);
    } else if (current.page == PAGE_RECORD && current.recording) {
        const unsigned elapsed = (unsigned)((esp_timer_get_time() -
                                   current.recording_started_us) / 1000000LL);
        const unsigned minutes = (elapsed / 60U) % 100U;
        const unsigned seconds = elapsed % 60U;
        char timer[8];
        snprintf(timer, sizeof(timer), "%02u:%02u", minutes, seconds);
        centered_text(176, timer, 2, COLOR_RED);
    }
    if (!current.online) {
        fill_rect(38, 93, 164, 54, RGB565(0x13, 0x13, 0x15));
        fill_rect(38, 93, 164, 3, COLOR_RED);
        fill_rect(38, 144, 164, 3, COLOR_RED);
        centered_text(108, "OFFLINE", 4, COLOR_RED);
    }
    esp_lcd_panel_io_tx_param(lcd_io, 0x2a, (uint8_t[]){0,0,0,239}, 4);
    esp_lcd_panel_io_tx_param(lcd_io, 0x2b, (uint8_t[]){0,0,0,239}, 4);
    esp_lcd_panel_io_tx_color(lcd_io, 0x2c, framebuffer, LCD_BYTES);
    xSemaphoreTake(lcd_done, pdMS_TO_TICKS(100));
}

static bool lcd_color_done(esp_lcd_panel_io_handle_t panel_io,
                           esp_lcd_panel_io_event_data_t *event_data,
                           void *context)
{
    (void)panel_io;
    (void)event_data;
    (void)context;
    BaseType_t task_woken = pdFALSE;
    xSemaphoreGiveFromISR(lcd_done, &task_woken);
    return task_woken == pdTRUE;
}

static void lcd_command(uint8_t command, const uint8_t *parameters, size_t count)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(lcd_io, command, parameters, count));
}

static void lcd_init(void)
{
    gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << LCD_RST_GPIO) | (1ULL << BOARD_POWER_GPIO_1) |
                        (1ULL << BOARD_POWER_GPIO_2),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&outputs));
    gpio_set_level(BOARD_POWER_GPIO_1, 1);
    gpio_set_level(BOARD_POWER_GPIO_2, 1);
    gpio_set_level(LCD_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(LCD_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    spi_bus_config_t bus = {
        .sclk_io_num = LCD_SCLK_GPIO, .mosi_io_num = LCD_MOSI_GPIO,
        .miso_io_num = -1, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = LCD_BYTES,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io = {
        .dc_gpio_num = LCD_DC_GPIO, .cs_gpio_num = LCD_CS_GPIO,
        .pclk_hz = 40 * 1000 * 1000, .lcd_cmd_bits = 8,
        .lcd_param_bits = 8, .spi_mode = 0, .trans_queue_depth = 1,
        .on_color_trans_done = lcd_color_done,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                              &io, &lcd_io));
    lcd_command(0xef,(uint8_t[]){0x00},1); lcd_command(0xeb,(uint8_t[]){0x14},1);
    lcd_command(0xfe,NULL,0); lcd_command(0xef,NULL,0);
    lcd_command(0x84,(uint8_t[]){0x40},1); lcd_command(0x85,(uint8_t[]){0xff},1);
    lcd_command(0x86,(uint8_t[]){0xff},1); lcd_command(0x87,(uint8_t[]){0xff},1);
    lcd_command(0x88,(uint8_t[]){0x0a},1); lcd_command(0x89,(uint8_t[]){0x21},1);
    lcd_command(0x8a,(uint8_t[]){0x00},1); lcd_command(0x8b,(uint8_t[]){0x80},1);
    lcd_command(0x8c,(uint8_t[]){0x01},1); lcd_command(0x8d,(uint8_t[]){0x01},1);
    lcd_command(0x8e,(uint8_t[]){0xff},1); lcd_command(0x8f,(uint8_t[]){0xff},1);
    lcd_command(0xb6,(uint8_t[]){0x00,0x20},2); lcd_command(0x36,(uint8_t[]){0x08},1);
    lcd_command(0x3a,(uint8_t[]){0x05},1);
    lcd_command(0x90,(uint8_t[]){0x08,0x08,0x08,0x08},4);
    lcd_command(0xbd,(uint8_t[]){0x06},1); lcd_command(0xbc,(uint8_t[]){0x00},1);
    lcd_command(0xff,(uint8_t[]){0x60,0x01,0x04},3);
    lcd_command(0xc3,(uint8_t[]){0x13},1); lcd_command(0xc4,(uint8_t[]){0x13},1);
    lcd_command(0xc9,(uint8_t[]){0x22},1); lcd_command(0xbe,(uint8_t[]){0x11},1);
    lcd_command(0xe1,(uint8_t[]){0x10,0x0e},2);
    lcd_command(0xdf,(uint8_t[]){0x21,0x0c,0x02},3);
    lcd_command(0xf0,(uint8_t[]){0x45,0x09,0x08,0x08,0x26,0x2a},6);
    lcd_command(0xf1,(uint8_t[]){0x43,0x70,0x72,0x36,0x37,0x6f},6);
    lcd_command(0xf2,(uint8_t[]){0x45,0x09,0x08,0x08,0x26,0x2a},6);
    lcd_command(0xf3,(uint8_t[]){0x43,0x70,0x72,0x36,0x37,0x6f},6);
    lcd_command(0xed,(uint8_t[]){0x1b,0x0b},2); lcd_command(0xae,(uint8_t[]){0x77},1);
    lcd_command(0xcd,(uint8_t[]){0x63},1);
    lcd_command(0x70,(uint8_t[]){0x07,0x07,0x04,0x0e,0x0f,0x09,0x07,0x08,0x03},9);
    lcd_command(0xe8,(uint8_t[]){0x34},1);
    lcd_command(0x62,(uint8_t[]){0x18,0x0d,0x71,0xed,0x70,0x70,0x18,0x0f,0x71,0xef,0x70,0x70},12);
    lcd_command(0x63,(uint8_t[]){0x18,0x11,0x71,0xf1,0x70,0x70,0x18,0x13,0x71,0xf3,0x70,0x70},12);
    lcd_command(0x64,(uint8_t[]){0x28,0x29,0xf1,0x01,0xf1,0x00,0x07},7);
    lcd_command(0x66,(uint8_t[]){0x3c,0x00,0xcd,0x67,0x45,0x45,0x10,0x00,0x00,0x00},10);
    lcd_command(0x67,(uint8_t[]){0x00,0x3c,0x00,0x00,0x00,0x01,0x54,0x10,0x32,0x98},10);
    lcd_command(0x74,(uint8_t[]){0x10,0x85,0x80,0x00,0x00,0x4e,0x00},7);
    lcd_command(0x98,(uint8_t[]){0x3e,0x07},2); lcd_command(0x21,NULL,0);
    lcd_command(0x11,NULL,0); vTaskDelay(pdMS_TO_TICKS(120));
    lcd_command(0x29,NULL,0); vTaskDelay(pdMS_TO_TICKS(20));
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t channel = {
        .gpio_num = LCD_BACKLIGHT_GPIO, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = 818,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

static void encoder_edge_isr(void *argument)
{
    (void)argument;
    const uint8_t levels = (uint8_t)((gpio_get_level(ENCODER_A_GPIO) << 1) |
                                      gpio_get_level(ENCODER_B_GPIO));
    BaseType_t task_woken = pdFALSE;
    xQueueSendFromISR(encoder_queue, &levels, &task_woken);
    if (task_woken) portYIELD_FROM_ISR();
}

static bool touch_pressed(int *x, int *y)
{
    uint8_t address = 0x01;
    uint8_t data[6] = {0};
    if (i2c_master_write_read_device(I2C_NUM_0, CST816_ADDRESS, &address, 1,
            data, sizeof(data), pdMS_TO_TICKS(20)) != ESP_OK ||
        (data[1] & 0x0f) == 0) return false;
    *x = ((data[2] & 0x0f) << 8) | data[3];
    *y = ((data[4] & 0x0f) << 8) | data[5];
    return true;
}

static void input_task(void *argument)
{
    (void)argument;
    static const int8_t transitions[16] = {
        0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0,
    };
    int previous = (gpio_get_level(ENCODER_A_GPIO) << 1) |
                   gpio_get_level(ENCODER_B_GPIO);
    int accumulator = 0;
    bool button_was_down = false;
    bool long_sent = false;
    int64_t button_down_at = 0;
    for (;;) {
        uint8_t current;
        while (xQueueReceive(encoder_queue, &current, 0) == pdTRUE) {
            accumulator += transitions[(previous << 2) | current];
            previous = current;
            if (abs(accumulator) >= 4) {
                atomic_store_explicit(&last_encoder_activity_us,
                                      esp_timer_get_time(),
                                      memory_order_relaxed);
                rotate_ui(accumulator > 0 ? 1 : -1);
                accumulator = 0;
            }
        }
        const bool button_down = gpio_get_level(ENCODER_BUTTON_GPIO) == 0;
        const int64_t now = esp_timer_get_time();
        if (button_down && !button_was_down) {
            atomic_store_explicit(&last_encoder_activity_us, now,
                                  memory_order_relaxed);
            button_down_at = now;
            long_sent = false;
        } else if (button_down && !long_sent &&
                   now - button_down_at >= LONG_PRESS_US) {
            go_home();
            long_sent = true;
        } else if (!button_down && button_was_down && !long_sent &&
                   now - button_down_at > 25000) {
            atomic_store_explicit(&last_encoder_activity_us, now,
                                  memory_order_relaxed);
            press_ui();
        }
        button_was_down = button_down;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void touch_task(void *argument)
{
    (void)argument;
    bool was_pressed = false;
    int start_x = 0, start_y = 0, last_x = 0, last_y = 0;
    for (;;) {
        int x = 0, y = 0;
        const bool pressed = gpio_get_level(TOUCH_INT_GPIO) == 0 &&
                             touch_pressed(&x, &y);
        if (pressed) {
            if (!was_pressed) start_x = x, start_y = y;
            last_x = x;
            last_y = y;
        } else if (was_pressed) {
            const int dx = last_x - start_x;
            const int dy = last_y - start_y;
            const int64_t since_encoder = esp_timer_get_time() -
                atomic_load_explicit(&last_encoder_activity_us,
                                     memory_order_relaxed);
            if (since_encoder >= 300000LL) {
                if (dy > SWIPE_THRESHOLD && abs(dy) > abs(dx)) go_back();
                else if (abs(dx) > SWIPE_THRESHOLD) {
                    rotate_ui(dx > 0 ? 1 : -1);
                }
            }
        }
        was_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void handle_message(const cluster_frame_view_t *view)
{
    switch (view->type) {
    case CLUSTER_MSG_HEARTBEAT:
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        state.last_heartbeat_us = esp_timer_get_time();
        state.online = true;
        xSemaphoreGive(state_mutex);
        break;
    case CLUSTER_MSG_AIR_RIDE_STATE: {
        cluster_air_ride_state_t air;
        if (cluster_air_ride_state_decode(view->payload, view->payload_size,
                                          &air) != CLUSTER_PROTOCOL_OK) {
            break;
        }
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        state.air_valid = air.pressure_valid;
        state.air_psi[0] = air.front_left_psi;
        state.air_psi[1] = air.front_right_psi;
        state.air_psi[2] = air.rear_left_psi;
        state.air_psi[3] = air.rear_right_psi;
        if (air.pressure_valid) {
            for (int target = 0; target < 7; ++target) {
                state.air_last[target] =
                    (uint16_t)air_start_value(&state, target);
            }
        }
        xSemaphoreGive(state_mutex);
        break;
    }
    case CLUSTER_MSG_LOGGING_STATUS: {
        cluster_logging_status_t logging;
        if (cluster_logging_status_decode(view->payload, view->payload_size,
                                          &logging) != CLUSTER_PROTOCOL_OK) {
            break;
        }
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        if (logging.recording && !state.recording) {
            state.recording_started_us = esp_timer_get_time();
        }
        state.logging_available = logging.available;
        state.recording = logging.available && logging.recording;
        if (state.page == PAGE_RECORD) {
            state.selection = state.recording ? 1 : 0;
        }
        xSemaphoreGive(state_mutex);
        break;
    }
    case CLUSTER_MSG_CLIMATE_STATE: {
        cluster_climate_state_t climate;
        if (cluster_climate_state_decode(view->payload, view->payload_size,
                                         &climate) != CLUSTER_PROTOCOL_OK ||
            !climate.connected) {
            break;
        }
        /* Follow the car's positions except while the user is adjusting. */
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        if (state.page != PAGE_FAN_ADJUST) state.fan_value = climate.target_fan;
        if (state.page != PAGE_TEMP_ADJUST) state.temp_value = climate.target_temp;
        xSemaphoreGive(state_mutex);
        break;
    }
    case CLUSTER_MSG_SETTINGS: {
        cluster_left_settings_t settings;
        if (cluster_left_settings_decode(view->payload, view->payload_size,
                                         &settings) != CLUSTER_PROTOCOL_OK) {
            break;
        }
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        state.theme = settings.theme;
        xSemaphoreGive(state_mutex);
        break;
    }
    default:
        break;
    }
}

static void network_task(void *argument)
{
    (void)argument;
    uint8_t frame[CLUSTER_PROTOCOL_MAX_FRAME_SIZE];
    for (;;) {
        size_t frame_size = 0;
        cluster_frame_view_t view;
        if (cluster_wireless_receive_message(frame, sizeof(frame), &frame_size,
                                             &view, pdMS_TO_TICKS(100)) ==
            ESP_OK) {
            handle_message(&view);
        }
        const int64_t now = esp_timer_get_time();
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        if (state.online && now - state.last_heartbeat_us >
                            CLUSTER_HEARTBEAT_TIMEOUT_MS * 1000LL) {
            state.online = false;
        }
        xSemaphoreGive(state_mutex);
    }
}

static void ui_task(void *argument)
{
    (void)argument;
    for (;;) {
        const int64_t now = esp_timer_get_time();
        bool idle_adjust = false;
        bool close_air = false;
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        if (now - state.last_activity_us >= IDLE_TIMEOUT_US) {
            idle_adjust = is_adjust_page(state.page);
            close_air = state.page == PAGE_AIR ||
                        state.page == PAGE_AIR_ADJUST;
            if (!idle_adjust) go_main_locked();
            state.last_activity_us = now;
        }
        xSemaphoreGive(state_mutex);
        if (idle_adjust) cancel_adjust();
        if (close_air) {
            xSemaphoreTake(state_mutex, portMAX_DELAY);
            go_main_locked();
            xSemaphoreGive(state_mutex);
            send_command(CLUSTER_KNOB_AIR_VIEW_SET, 0, 0,
                         CLUSTER_KNOB_COMMAND_CONFIRM);
        }
        render();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void peripherals_init(void)
{
    gpio_config_t inputs = {
        .pin_bit_mask = (1ULL << ENCODER_A_GPIO) | (1ULL << ENCODER_B_GPIO) |
                        (1ULL << ENCODER_BUTTON_GPIO) | (1ULL << TOUCH_INT_GPIO),
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&inputs));
    ESP_ERROR_CHECK(gpio_set_intr_type(ENCODER_A_GPIO, GPIO_INTR_ANYEDGE));
    ESP_ERROR_CHECK(gpio_set_intr_type(ENCODER_B_GPIO, GPIO_INTR_ANYEDGE));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(ENCODER_A_GPIO, encoder_edge_isr, NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add(ENCODER_B_GPIO, encoder_edge_isr, NULL));
    gpio_config_t touch_reset = {
        .pin_bit_mask = 1ULL << TOUCH_RST_GPIO, .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&touch_reset));
    gpio_set_level(TOUCH_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(TOUCH_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    i2c_config_t i2c = {
        .mode = I2C_MODE_MASTER, .sda_io_num = TOUCH_SDA_GPIO,
        .scl_io_num = TOUCH_SCL_GPIO, .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE, .master.clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
}

void app_main(void)
{
    lcd_done = xSemaphoreCreateBinary();
    state_mutex = xSemaphoreCreateMutex();
    encoder_queue = xQueueCreate(64, sizeof(uint8_t));
    framebuffer = heap_caps_malloc(LCD_BYTES, MALLOC_CAP_DMA);
    ESP_ERROR_CHECK(lcd_done && state_mutex && encoder_queue && framebuffer ?
                    ESP_OK : ESP_ERR_NO_MEM);
    lcd_init();
    peripherals_init();
    state.last_activity_us = esp_timer_get_time();
    const cluster_wireless_config_t wireless = {
        .role = CLUSTER_WIRELESS_ROLE_KNOB,
        .initialize_wifi = true,
        .connect_to_center = true,
    };
    ESP_ERROR_CHECK(cluster_wireless_start(&wireless));
    ESP_LOGI(TAG, "MackoDash knob v0.1.0 joining channel %u",
             CLUSTER_WIRELESS_CHANNEL);
    ESP_ERROR_CHECK(xTaskCreate(input_task, "knob_input", 4096, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(xTaskCreate(touch_task, "knob_touch", 4096, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(xTaskCreate(network_task, "knob_network", 4096, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(xTaskCreate(ui_task, "knob_ui", 4096, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_FAIL);
}