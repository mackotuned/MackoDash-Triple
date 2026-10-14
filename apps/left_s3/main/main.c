#include <inttypes.h>

#include "cluster_protocol.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "triple_left";

static bool protocol_self_test(void)
{
    uint8_t frame[CLUSTER_PROTOCOL_MAX_FRAME_SIZE];
    size_t frame_size = 0;
    cluster_frame_view_t view = {0};

    return cluster_frame_encode(CLUSTER_MSG_DISPLAY_HEALTH, 1U, 0U, NULL, 0U,
                                frame, sizeof(frame), &frame_size) ==
               CLUSTER_PROTOCOL_OK &&
           cluster_frame_decode(frame, frame_size, &view) == CLUSTER_PROTOCOL_OK &&
           view.type == CLUSTER_MSG_DISPLAY_HEALTH;
}

void app_main(void)
{
    ESP_LOGI(TAG, "MackoDash Triple left S3 v0.1.0");
    ESP_LOGI(TAG, "Role: display-only UART node");
    ESP_LOGI(TAG, "Cluster protocol self-test: %s",
             protocol_self_test() ? "PASS" : "FAIL");

    for (;;) {
        ESP_LOGI(TAG, "Awaiting center P4, uptime=%" PRIu32 " ms",
                 (uint32_t)(esp_timer_get_time() / 1000));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
