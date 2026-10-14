#include <inttypes.h>

#include "cluster_protocol.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings_schema.h"
#include "vehicle_data.h"

static const char *TAG = "triple_center";

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
    ESP_LOGI(TAG, "Cluster protocol self-test: %s",
             protocol_self_test() ? "PASS" : "FAIL");

    for (;;) {
        ESP_LOGI(TAG, "Heartbeat uptime=%" PRIu32 " ms",
                 (uint32_t)(esp_timer_get_time() / 1000));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
