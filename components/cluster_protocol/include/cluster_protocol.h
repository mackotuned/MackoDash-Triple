#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLUSTER_PROTOCOL_MAGIC 0x4D44U
#define CLUSTER_PROTOCOL_VERSION 1U
#define CLUSTER_PROTOCOL_HEADER_SIZE 14U
#define CLUSTER_PROTOCOL_MAX_PAYLOAD 256U
#define CLUSTER_PROTOCOL_MAX_FRAME_SIZE \
    (CLUSTER_PROTOCOL_HEADER_SIZE + CLUSTER_PROTOCOL_MAX_PAYLOAD)

typedef enum {
    CLUSTER_MSG_HEARTBEAT = 0x01,
    CLUSTER_MSG_VEHICLE_FAST = 0x02,
    CLUSTER_MSG_VEHICLE_SLOW = 0x03,
    CLUSTER_MSG_WARNINGS = 0x04,
    CLUSTER_MSG_SETTINGS = 0x05,
} cluster_message_type_t;

typedef enum {
    CLUSTER_PROTOCOL_OK = 0,
    CLUSTER_PROTOCOL_INVALID_ARGUMENT,
    CLUSTER_PROTOCOL_BUFFER_TOO_SMALL,
    CLUSTER_PROTOCOL_BAD_MAGIC,
    CLUSTER_PROTOCOL_BAD_VERSION,
    CLUSTER_PROTOCOL_BAD_LENGTH,
    CLUSTER_PROTOCOL_BAD_CRC,
} cluster_protocol_result_t;

typedef struct {
    cluster_message_type_t type;
    uint16_t sequence;
    uint32_t uptime_ms;
    const uint8_t *payload;
    uint16_t payload_size;
} cluster_frame_view_t;

uint16_t cluster_crc16_ccitt(const uint8_t *data, size_t size);

cluster_protocol_result_t cluster_frame_encode(
    cluster_message_type_t type,
    uint16_t sequence,
    uint32_t uptime_ms,
    const void *payload,
    uint16_t payload_size,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_size);

cluster_protocol_result_t cluster_frame_decode(
    const uint8_t *frame,
    size_t frame_size,
    cluster_frame_view_t *view);

#ifdef __cplusplus
}
#endif
