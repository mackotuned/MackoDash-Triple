#include "cluster_uart.h"

#include <string.h>

#define CLUSTER_MAGIC_LOW_BYTE ((uint8_t)(CLUSTER_PROTOCOL_MAGIC & 0xFFU))
#define CLUSTER_MAGIC_HIGH_BYTE ((uint8_t)(CLUSTER_PROTOCOL_MAGIC >> 8U))
#define CLUSTER_PAYLOAD_SIZE_OFFSET 6U
#define CLUSTER_UART_DRIVER_RX_BUFFER_SIZE 1024

static esp_err_t configure_uart(uart_port_t port, int baud_rate)
{
    const uart_config_t config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    return uart_param_config(port, &config);
}

esp_err_t cluster_uart_init_tx(uart_port_t port, int tx_gpio, int baud_rate)
{
    esp_err_t result = configure_uart(port, baud_rate);
    if (result != ESP_OK) {
        return result;
    }

    result = uart_set_pin(port, tx_gpio, UART_PIN_NO_CHANGE,
                          UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (result != ESP_OK) {
        return result;
    }
    return uart_driver_install(port, 0, 0, 0, NULL, 0);
}

esp_err_t cluster_uart_init_rx(cluster_uart_receiver_t *receiver,
                               uart_port_t port,
                               int rx_gpio,
                               int baud_rate)
{
    if (receiver == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = configure_uart(port, baud_rate);
    if (result != ESP_OK) {
        return result;
    }

    result = uart_set_pin(port, UART_PIN_NO_CHANGE, rx_gpio,
                          UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (result != ESP_OK) {
        return result;
    }

    result = uart_driver_install(port, CLUSTER_UART_DRIVER_RX_BUFFER_SIZE,
                                 0, 0, NULL, 0);
    if (result == ESP_OK) {
        receiver->port = port;
        receiver->buffered_size = 0U;
    }
    return result;
}

esp_err_t cluster_uart_send(uart_port_t port,
                            const uint8_t *frame,
                            size_t frame_size)
{
    if (frame == NULL || frame_size < CLUSTER_PROTOCOL_HEADER_SIZE ||
        frame_size > CLUSTER_PROTOCOL_MAX_FRAME_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    const int written = uart_write_bytes(port, frame, frame_size);
    return written == (int)frame_size ? ESP_OK : ESP_FAIL;
}

static void discard_prefix(cluster_uart_receiver_t *receiver, size_t count)
{
    if (count >= receiver->buffered_size) {
        receiver->buffered_size = 0U;
        return;
    }

    receiver->buffered_size -= count;
    memmove(receiver->buffer, &receiver->buffer[count], receiver->buffered_size);
}

static esp_err_t extract_frame(cluster_uart_receiver_t *receiver,
                               uint8_t *frame,
                               size_t frame_capacity,
                               size_t *frame_size)
{
    while (receiver->buffered_size >= 2U) {
        size_t magic_offset = 0U;
        while (magic_offset + 1U < receiver->buffered_size &&
               (receiver->buffer[magic_offset] != CLUSTER_MAGIC_LOW_BYTE ||
                receiver->buffer[magic_offset + 1U] != CLUSTER_MAGIC_HIGH_BYTE)) {
            ++magic_offset;
        }
        if (magic_offset != 0U) {
            discard_prefix(receiver, magic_offset);
        }
        if (receiver->buffered_size < CLUSTER_PROTOCOL_HEADER_SIZE) {
            return ESP_ERR_NOT_FOUND;
        }

        const uint16_t payload_size =
            (uint16_t)receiver->buffer[CLUSTER_PAYLOAD_SIZE_OFFSET] |
            ((uint16_t)receiver->buffer[CLUSTER_PAYLOAD_SIZE_OFFSET + 1U] << 8U);
        if (payload_size > CLUSTER_PROTOCOL_MAX_PAYLOAD) {
            discard_prefix(receiver, 1U);
            continue;
        }

        const size_t candidate_size = CLUSTER_PROTOCOL_HEADER_SIZE + payload_size;
        if (receiver->buffered_size < candidate_size) {
            return ESP_ERR_NOT_FOUND;
        }
        if (frame_capacity < candidate_size) {
            return ESP_ERR_INVALID_SIZE;
        }

        cluster_frame_view_t view = {0};
        if (cluster_frame_decode(receiver->buffer, candidate_size, &view) !=
            CLUSTER_PROTOCOL_OK) {
            discard_prefix(receiver, 1U);
            continue;
        }

        memcpy(frame, receiver->buffer, candidate_size);
        *frame_size = candidate_size;
        discard_prefix(receiver, candidate_size);
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t cluster_uart_receive_frame(cluster_uart_receiver_t *receiver,
                                     uint8_t *frame,
                                     size_t frame_capacity,
                                     size_t *frame_size,
                                     TickType_t timeout_ticks)
{
    if (receiver == NULL || frame == NULL || frame_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = extract_frame(receiver, frame, frame_capacity, frame_size);
    if (result != ESP_ERR_NOT_FOUND) {
        return result;
    }

    const size_t available = sizeof(receiver->buffer) - receiver->buffered_size;
    const int received = uart_read_bytes(
        receiver->port, &receiver->buffer[receiver->buffered_size], available,
        timeout_ticks);
    if (received < 0) {
        return ESP_FAIL;
    }
    if (received == 0) {
        return ESP_ERR_TIMEOUT;
    }

    receiver->buffered_size += (size_t)received;
    result = extract_frame(receiver, frame, frame_capacity, frame_size);
    return result == ESP_ERR_NOT_FOUND ? ESP_ERR_TIMEOUT : result;
}