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
    ESP_LOGI(TAG, "Display links: GPIO%d -> left RXD, GPIO%d -> right RXD at %d baud",
             CLUSTER_CENTER_LEFT_TX_GPIO, CLUSTER_CENTER_RIGHT_TX_GPIO,
             CLUSTER_UART_BAUD_RATE);
    ESP_LOGI(TAG, "Cluster protocol self-test: %s",
             protocol_self_test() ? "PASS" : "FAIL");

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
