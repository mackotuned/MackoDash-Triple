#include "log_overlay.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>

#include "center_ui.h"
#include "hondata_logger.h"

#define LOG_VALUE_COUNT 26U
#define LOG_ROWS 13U

static lv_obj_t *overlay;
static lv_obj_t *status_label;
static lv_obj_t *value_labels[LOG_VALUE_COUNT];
static bool shown_recording;
static bool status_color_valid;

void log_overlay_create(lv_obj_t *screen)
{
    static const char *const names[LOG_VALUE_COUNT] = {
        "RPM", "SPEED", "GEAR", "BATTERY", "IAT", "COOLANT",
        "THROTTLE", "MAP", "INJECTOR", "IGNITION", "AFR", "KNOCK",
        "CAM TARGET", "CAM ACTUAL", "ANALOG 1", "ANALOG 2", "ANALOG 3",
        "ANALOG 4", "ANALOG 5", "ANALOG 6", "ANALOG 7", "ANALOG 8",
        "ETHANOL HZ", "ETHANOL DUTY", "ETHANOL %", "FRAME MASK",
    };
    overlay = center_ui_panel_create(screen, 0x111113);

    lv_obj_t *title = center_ui_label_create(
        overlay, "HONDATA LOG", &lv_font_montserrat_48, 0xECECE7);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 44);
    status_label = center_ui_label_create(
        overlay, "READY", &lv_font_montserrat_14, 0xE81E1E);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 88);

    for (size_t index = 0; index < LOG_VALUE_COUNT; ++index) {
        const int32_t x = 64 + (int32_t)(index / LOG_ROWS) * 372;
        const int32_t y = 138 + (int32_t)(index % LOG_ROWS) * 45;
        lv_obj_t *name = center_ui_label_create(
            overlay, names[index], &lv_font_montserrat_14, 0x9A9A96);
        lv_obj_set_pos(name, x, y);
        lv_obj_set_size(name, 190, 30);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        value_labels[index] = center_ui_label_create(
            overlay, "--", &lv_font_montserrat_24, 0xECECE7);
        lv_obj_set_pos(value_labels[index], x + 190, y);
        lv_obj_set_size(value_labels[index], 118, 32);
        lv_obj_set_style_text_align(value_labels[index], LV_TEXT_ALIGN_RIGHT,
                                    LV_PART_MAIN);
    }
}

static void set_value(size_t index, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

static void set_value(size_t index, const char *format, ...)
{
    char text[24];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    center_ui_label_set_text(value_labels[index], text);
}

void log_overlay_update(bool open, const hondata_state_t *live_state)
{
    lv_obj_set_hidden(overlay, !open);
    if (!open) return;

    hondata_state_t state;
    uint32_t sample_ms = 0U;
    const bool logged = hondata_logger_get_latest(&state, &sample_ms);
    if (!logged) state = *live_state;
    hondata_logger_status_t status;
    hondata_logger_get_status(&status);

    char text[64];
    snprintf(text, sizeof(text), "%s  SESSION %04u  %" PRIu32 " SAMPLES",
             status.recording ? "RECORDING" : (logged ? "LATEST" : "LIVE"),
             (unsigned)status.session_index, status.record_count);
    center_ui_label_set_text(status_label, text);
    if (!status_color_valid || shown_recording != status.recording) {
        lv_obj_set_style_text_color(
            status_label, lv_color_hex(status.recording ? 0xE81E1E : 0xECECE7),
            LV_PART_MAIN);
        shown_recording = status.recording;
        status_color_valid = true;
    }

    set_value(0, "%u", (unsigned)state.rpm);
    set_value(1, "%.1f KPH", state.speed_tenths_kph / 10.0);
    set_value(2, "%u", (unsigned)state.gear);
    set_value(3, "%.1f V", state.battery_mv / 1000.0);
    set_value(4, "%.1f C", state.intake_temp_tenths_c / 10.0);
    set_value(5, "%.1f C", state.coolant_temp_tenths_c / 10.0);
    set_value(6, "%.1f %%", state.throttle_tenths_percent / 10.0);
    set_value(7, "%.1f KPA", state.map_gauge_tenths_kpa / 10.0);
    set_value(8, "%u", (unsigned)state.injector_duration_raw);
    set_value(9, "%d DEG", (int)state.ignition_angle_deg);
    set_value(10, "%.3f", state.afr_thousandths / 1000.0);
    set_value(11, "%u", (unsigned)state.knock_count);
    set_value(12, "%d DEG", (int)state.cam_angle_target_deg);
    set_value(13, "%d DEG", (int)state.cam_angle_actual_deg);
    for (size_t index = 0; index < 8U; ++index) {
        set_value(14U + index, "%u MV", (unsigned)state.analog_mv[index]);
    }
    set_value(22, "%u HZ", (unsigned)state.ethanol_frequency_hz);
    set_value(23, "%.2f %%", state.ethanol_duty_hundredths_percent / 100.0);
    set_value(24, "%.1f %%", state.ethanol_content_tenths_percent / 10.0);
    set_value(25, "0x%03" PRIX32, state.received_frame_mask & 0x3ffU);
}
