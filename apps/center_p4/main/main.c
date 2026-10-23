#include "air_link.h"
#include "bsp/esp-bsp.h"
#include "center_display.h"
#include "center_link.h"
#include "cluster_board_config.h"
#include "cluster_protocol.h"
#include "cluster_wireless.h"
#include "esp_check.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hondata_can.h"
#include "hondata_logger.h"
#include "odometer.h"
#include "settings_service.h"
#include "simulation.h"
#include "vehicle_data.h"

#define MAIN_LOOP_PERIOD_MS 20U
#define VEHICLE_FAST_PERIOD_MS 40U
#define STATUS_PERIOD_MS 1000U
#define ODOMETER_SAVE_PERIOD_MS 3000U

static const char *TAG = "triple_center";

/* Decodes known frames and checks every converted channel. */
static bool hondata_self_test(void)
{
    hondata_state_t state = {0};
    vehicle_fast_state_t vehicle = {0};
    const uint8_t frame_660[] = {0x1B, 0x58, 0x00, 0x64, 0x03, 0x8E};
    const uint8_t frame_661[] = {0x00, 0x19, 0x00, 0x5A};
    const uint8_t frame_662[] = {0x00, 0x4E, 0x07, 0xE4};
    const uint8_t frame_664[] = {0x00, 0x00, 0x80, 0x00};

    if (hondata_decode_frame(&state, 0x660, frame_660, sizeof(frame_660), 10) !=
            HONDATA_DECODE_OK ||
        hondata_decode_frame(&state, 0x661, frame_661, sizeof(frame_661), 20) !=
            HONDATA_DECODE_OK ||
        hondata_decode_frame(&state, 0x662, frame_662, sizeof(frame_662), 30) !=
            HONDATA_DECODE_OK ||
        hondata_decode_frame(&state, 0x664, frame_664, sizeof(frame_664), 40) !=
            HONDATA_DECODE_OK) {
        return false;
    }

    hondata_apply_vehicle_fast(&state, &vehicle);
    return state.gear == 3U && state.ignition_angle_deg == 0 &&
           vehicle.rpm == 7000U && vehicle.speed_tenths_kph == 1000U &&
           vehicle.battery_mv == 14200U &&
           vehicle.intake_tenths_c == 250 &&
           vehicle.coolant_tenths_c == 900 &&
           vehicle.throttle_tenths_percent == 780U &&
           vehicle.boost_tenths_kpa == 1007 &&
           vehicle.afr_thousandths == 14692U;
}

static void start_logging(void)
{
    esp_err_t error = bsp_sdcard_mount();
    if (error == ESP_OK) error = hondata_logger_init(BSP_SD_MOUNT_POINT);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "SD card unavailable; recording disabled: %s",
                 esp_err_to_name(error));
        return;
    }
    ESP_LOGI(TAG, "Hondata logs will be stored on the onboard SD card");
    cluster_settings_t settings;
    settings_service_get(&settings);
    if (settings.logging.auto_record) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(hondata_logger_set_recording(
            true, (uint8_t)settings.logging.log_name,
            (uint32_t)(esp_timer_get_time() / 1000)));
    }
}

static cluster_vehicle_fast_t to_wireless(const vehicle_fast_state_t *vehicle,
                                          bool data_valid)
{
    return (cluster_vehicle_fast_t){
        .sample_time_ms = vehicle->sample_time_ms,
        .rpm = vehicle->rpm,
        .speed_tenths_kph = vehicle->speed_tenths_kph,
        .boost_tenths_kpa = vehicle->boost_tenths_kpa,
        .coolant_tenths_c = vehicle->coolant_tenths_c,
        .intake_tenths_c = vehicle->intake_tenths_c,
        .afr_thousandths = vehicle->afr_thousandths,
        .oil_pressure_tenths_kpa = vehicle->oil_pressure_tenths_kpa,
        .fuel_pressure_tenths_kpa = vehicle->fuel_pressure_tenths_kpa,
        .battery_mv = vehicle->battery_mv,
        .throttle_tenths_percent = vehicle->throttle_tenths_percent,
        .fuel_level_tenths_percent = vehicle->fuel_level_tenths_percent,
        .ignition_deg = vehicle->ignition_deg,
        .ethanol_tenths_percent = vehicle->ethanol_tenths_percent,
        .knock_count = vehicle->knock_count,
        .valid_channels = data_valid ? vehicle->valid_channels : 0U,
        .warning_channels = vehicle->warning_channels,
    };
}

void app_main(void)
{
    ESP_LOGI(TAG, "MackoDash Triple center P4 v0.1.0");
    ESP_LOGI(TAG, "Role: CAN/settings/control authority and center display");
    ESP_LOGI(TAG, "Display links: channel-%u MackoDash wireless network",
             CLUSTER_WIRELESS_CHANNEL);
    ESP_LOGI(TAG, "Cluster protocol self-test: %s",
             cluster_protocol_self_test() ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "Hondata decoder self-test: %s",
             hondata_self_test() ? "PASS" : "FAIL");

    ESP_ERROR_CHECK(esp_hosted_slave_reset());
    ESP_ERROR_CHECK(settings_service_start(center_display_settings_changed,
                                           NULL, air_link_web_state,
                                           air_link_web_command, NULL));
    center_display_start();
    const cluster_wireless_config_t wireless_config = {
        .role = CLUSTER_WIRELESS_ROLE_CENTER,
        .initialize_wifi = false,
        .connect_to_center = false,
    };
    ESP_ERROR_CHECK(cluster_wireless_start(&wireless_config));
    ESP_ERROR_CHECK(odometer_init());
    ESP_ERROR_CHECK(hondata_can_start());
    start_logging();
    ESP_LOGI(TAG, "Hondata CAN: RX GPIO%d, TX GPIO%d, automatic bitrate scan",
             CLUSTER_CENTER_CAN_RX_GPIO, CLUSTER_CENTER_CAN_TX_GPIO);
    center_link_start();

    uint32_t last_vehicle_fast_ms = 0U;
    uint32_t last_status_ms = 0U;
    uint32_t last_odometer_save_ms = 0U;

    for (;;) {
        const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        vehicle_fast_state_t vehicle;
        hondata_state_t hondata;
        hondata_can_diagnostics_t diagnostics;
        hondata_can_get_vehicle_fast(&vehicle);
        hondata_can_get_state(&hondata);
        hondata_can_get_diagnostics(&diagnostics);
        hondata_logger_sample(&hondata, now_ms);

        cluster_settings_t settings;
        settings_service_get(&settings);
        hondata_apply_fuel_level(&hondata, settings.fuel.analog_input,
                                 settings.fuel.empty_mv, settings.fuel.full_mv,
                                 &vehicle);

        const bool speed_valid = diagnostics.live_data &&
            vehicle_data_channel_valid(vehicle.valid_channels,
                                       VEHICLE_DATA_SPEED);
        odometer_update(vehicle.speed_tenths_kph, speed_valid, now_ms);
        if (now_ms - last_odometer_save_ms >= ODOMETER_SAVE_PERIOD_MS) {
            last_odometer_save_ms = now_ms;
            ESP_ERROR_CHECK_WITHOUT_ABORT(odometer_periodic_save());
        }

        const bool simulation_enabled = settings_service_simulation_enabled();
        if (simulation_enabled) simulation_fill_vehicle(&vehicle, now_ms);
        if (now_ms - last_vehicle_fast_ms >= VEHICLE_FAST_PERIOD_MS) {
            last_vehicle_fast_ms = now_ms;
            const cluster_vehicle_fast_t wireless = to_wireless(
                &vehicle, simulation_enabled || diagnostics.live_data);
            center_link_send_vehicle(&wireless);
        }
        if (now_ms - last_status_ms >= STATUS_PERIOD_MS) {
            last_status_ms = now_ms;
            center_link_send_status();
        }
        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_PERIOD_MS));
    }
}
