#include <inttypes.h>

#include "cluster_board_config.h"
#include "cluster_protocol.h"
#include "cluster_uart.h"
#include "driver/uart.h"
#include "esp_check.h"
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

    return cluster_frame_encode(CLUSTER_MSG_HEARTBEAT, 1U, 0U, NULL, 0U,
                                frame, sizeof(frame), &frame_size) ==
               CLUSTER_PROTOCOL_OK &&
           cluster_frame_decode(frame, frame_size, &view) == CLUSTER_PROTOCOL_OK &&
           view.type == CLUSTER_MSG_HEARTBEAT;
}

void app_main(void)
{
    ESP_LOGI(TAG, "MackoDash Triple left S3 v0.1.0");
    ESP_LOGI(TAG, "Role: display-only UART node");
    ESP_LOGI(TAG, "Cluster protocol self-test: %s",
             protocol_self_test() ? "PASS" : "FAIL");

    cluster_uart_receiver_t receiver = {0};
    ESP_ERROR_CHECK(cluster_uart_init_rx(
        &receiver, (uart_port_t)CLUSTER_SIDE_UART_PORT,
        CLUSTER_SIDE_RX_GPIO, CLUSTER_UART_BAUD_RATE));
    ESP_LOGI(TAG, "Listening on RXD/GPIO%d at %d baud",
             CLUSTER_SIDE_RX_GPIO, CLUSTER_UART_BAUD_RATE);

    uint8_t frame[CLUSTER_PROTOCOL_MAX_FRAME_SIZE];

    for (;;) {
        size_t frame_size = 0U;
        if (cluster_uart_receive_frame(&receiver, frame, sizeof(frame),
                                       &frame_size, pdMS_TO_TICKS(250)) == ESP_OK) {
            cluster_frame_view_t view = {0};
            if (cluster_frame_decode(frame, frame_size, &view) == CLUSTER_PROTOCOL_OK) {
                ESP_LOGI(TAG, "Frame type=%u sequence=%u center_uptime=%" PRIu32,
                         (unsigned)view.type, view.sequence, view.uptime_ms);
            }
        }
    }
}
