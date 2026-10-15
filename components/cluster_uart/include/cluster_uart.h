#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "cluster_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLUSTER_UART_RECEIVE_BUFFER_SIZE (CLUSTER_PROTOCOL_MAX_FRAME_SIZE * 2U)

typedef struct {
    uart_port_t port;
    uint8_t buffer[CLUSTER_UART_RECEIVE_BUFFER_SIZE];
    size_t buffered_size;
} cluster_uart_receiver_t;

esp_err_t cluster_uart_init_tx(uart_port_t port, int tx_gpio, int baud_rate);
esp_err_t cluster_uart_init_rx(cluster_uart_receiver_t *receiver,
                               uart_port_t port,
                               int rx_gpio,
                               int baud_rate);
esp_err_t cluster_uart_send(uart_port_t port,
                            const uint8_t *frame,
                            size_t frame_size);
esp_err_t cluster_uart_receive_frame(cluster_uart_receiver_t *receiver,
                                     uint8_t *frame,
                                     size_t frame_capacity,
                                     size_t *frame_size,
                                     TickType_t timeout_ticks);

#ifdef __cplusplus
}
#endif