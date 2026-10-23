#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cluster_protocol.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLUSTER_WIRELESS_CHANNEL 6U

typedef enum {
    CLUSTER_WIRELESS_ROLE_CENTER = 1U << 0,
    CLUSTER_WIRELESS_ROLE_LEFT = 1U << 1,
    CLUSTER_WIRELESS_ROLE_RIGHT = 1U << 2,
    CLUSTER_WIRELESS_ROLE_KNOB = 1U << 3,
    CLUSTER_WIRELESS_ROLE_SIDES = CLUSTER_WIRELESS_ROLE_LEFT |
                                  CLUSTER_WIRELESS_ROLE_RIGHT,
    CLUSTER_WIRELESS_ROLE_ALL = CLUSTER_WIRELESS_ROLE_CENTER |
                                CLUSTER_WIRELESS_ROLE_SIDES |
                                CLUSTER_WIRELESS_ROLE_KNOB,
} cluster_wireless_role_t;

typedef struct {
    cluster_wireless_role_t role;
    bool initialize_wifi;
    bool connect_to_center;
    /* Optional station MAC to adopt (only with initialize_wifi). */
    const uint8_t *station_mac;
} cluster_wireless_config_t;

esp_err_t cluster_wireless_start(const cluster_wireless_config_t *config);
bool cluster_wireless_ready(void);
esp_err_t cluster_wireless_send(cluster_wireless_role_t destination,
                                const void *frame, size_t frame_size);
esp_err_t cluster_wireless_receive(void *frame, size_t frame_capacity,
                                   size_t *frame_size, TickType_t timeout);

/* Wraps an encoded payload in a cluster frame and sends it. Returns
 * ESP_ERR_INVALID_STATE while the link is down. */
esp_err_t cluster_wireless_send_message(cluster_wireless_role_t destination,
                                        cluster_message_type_t type,
                                        const void *payload,
                                        size_t payload_size);

/* Receives one frame into buffer and decodes it; view points into buffer.
 * Malformed frames return ESP_ERR_INVALID_RESPONSE. */
esp_err_t cluster_wireless_receive_message(uint8_t *buffer, size_t capacity,
                                           size_t *frame_size,
                                           cluster_frame_view_t *view,
                                           TickType_t timeout);

#ifdef __cplusplus
}
#endif