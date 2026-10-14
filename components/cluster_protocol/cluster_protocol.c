#include "cluster_protocol.h"

#include <string.h>

static void write_u16_le(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value & 0xFFU);
    output[1] = (uint8_t)(value >> 8U);
}

static void write_u32_le(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value & 0xFFU);
    output[1] = (uint8_t)((value >> 8U) & 0xFFU);
    output[2] = (uint8_t)((value >> 16U) & 0xFFU);
    output[3] = (uint8_t)(value >> 24U);
}

static uint16_t read_u16_le(const uint8_t *input)
{
    return (uint16_t)input[0] | ((uint16_t)input[1] << 8U);
}

static uint32_t read_u32_le(const uint8_t *input)
{
    return (uint32_t)input[0] |
           ((uint32_t)input[1] << 8U) |
           ((uint32_t)input[2] << 16U) |
           ((uint32_t)input[3] << 24U);
}

static uint16_t crc16_update(uint16_t crc, const uint8_t *data, size_t size)
{
    for (size_t index = 0; index < size; ++index) {
        crc ^= (uint16_t)data[index] << 8U;
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            crc = (crc & 0x8000U) != 0U
                      ? (uint16_t)((crc << 1U) ^ 0x1021U)
                      : (uint16_t)(crc << 1U);
        }
    }
    return crc;
}

uint16_t cluster_crc16_ccitt(const uint8_t *data, size_t size)
{
    if (data == NULL && size != 0U) {
        return 0U;
    }
    return crc16_update(0xFFFFU, data, size);
}

cluster_protocol_result_t cluster_frame_encode(
    cluster_message_type_t type,
    uint16_t sequence,
    uint32_t uptime_ms,
    const void *payload,
    uint16_t payload_size,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_size)
{
    if (output == NULL || output_size == NULL ||
        (payload == NULL && payload_size != 0U)) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }
    if (payload_size > CLUSTER_PROTOCOL_MAX_PAYLOAD) {
        return CLUSTER_PROTOCOL_BAD_LENGTH;
    }

    const size_t frame_size = CLUSTER_PROTOCOL_HEADER_SIZE + payload_size;
    if (output_capacity < frame_size) {
        return CLUSTER_PROTOCOL_BUFFER_TOO_SMALL;
    }

    write_u16_le(&output[0], CLUSTER_PROTOCOL_MAGIC);
    output[2] = CLUSTER_PROTOCOL_VERSION;
    output[3] = (uint8_t)type;
    write_u16_le(&output[4], sequence);
    write_u16_le(&output[6], payload_size);
    write_u32_le(&output[8], uptime_ms);
    write_u16_le(&output[12], 0U);

    if (payload_size != 0U) {
        memcpy(&output[CLUSTER_PROTOCOL_HEADER_SIZE], payload, payload_size);
    }

    uint16_t crc = crc16_update(0xFFFFU, output, 12U);
    crc = crc16_update(crc, &output[CLUSTER_PROTOCOL_HEADER_SIZE], payload_size);
    write_u16_le(&output[12], crc);
    *output_size = frame_size;
    return CLUSTER_PROTOCOL_OK;
}

cluster_protocol_result_t cluster_frame_decode(
    const uint8_t *frame,
    size_t frame_size,
    cluster_frame_view_t *view)
{
    if (frame == NULL || view == NULL) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }
    if (frame_size < CLUSTER_PROTOCOL_HEADER_SIZE) {
        return CLUSTER_PROTOCOL_BAD_LENGTH;
    }
    if (read_u16_le(&frame[0]) != CLUSTER_PROTOCOL_MAGIC) {
        return CLUSTER_PROTOCOL_BAD_MAGIC;
    }
    if (frame[2] != CLUSTER_PROTOCOL_VERSION) {
        return CLUSTER_PROTOCOL_BAD_VERSION;
    }

    const uint16_t payload_size = read_u16_le(&frame[6]);
    if (payload_size > CLUSTER_PROTOCOL_MAX_PAYLOAD ||
        frame_size != CLUSTER_PROTOCOL_HEADER_SIZE + payload_size) {
        return CLUSTER_PROTOCOL_BAD_LENGTH;
    }

    uint16_t crc = crc16_update(0xFFFFU, frame, 12U);
    crc = crc16_update(crc, &frame[CLUSTER_PROTOCOL_HEADER_SIZE], payload_size);
    if (crc != read_u16_le(&frame[12])) {
        return CLUSTER_PROTOCOL_BAD_CRC;
    }

    view->type = (cluster_message_type_t)frame[3];
    view->sequence = read_u16_le(&frame[4]);
    view->uptime_ms = read_u32_le(&frame[8]);
    view->payload = &frame[CLUSTER_PROTOCOL_HEADER_SIZE];
    view->payload_size = payload_size;
    return CLUSTER_PROTOCOL_OK;
}
