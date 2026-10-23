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

static bool knob_command_valid(uint8_t code, uint8_t flags)
{
    return code >= (uint8_t)CLUSTER_KNOB_COMMAND_FIRST &&
           code <= (uint8_t)CLUSTER_KNOB_COMMAND_LAST &&
           (flags & ~CLUSTER_KNOB_COMMAND_FLAGS) == 0U;
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

cluster_protocol_result_t cluster_air_ride_state_encode(
    const cluster_air_ride_state_t *state,
    uint8_t *payload,
    size_t payload_capacity)
{
    if (state == NULL || payload == NULL) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }
    if (payload_capacity < CLUSTER_AIR_RIDE_STATE_PAYLOAD_SIZE) {
        return CLUSTER_PROTOCOL_BUFFER_TOO_SMALL;
    }

    payload[0] = CLUSTER_AIR_RIDE_PAYLOAD_VERSION;
    payload[1] = (state->connected ? 0x01U : 0U) |
                 (state->pressure_valid ? 0x02U : 0U) |
                 (state->compressor_valid ? 0x04U : 0U) |
                 (state->compressor_on ? 0x08U : 0U) |
                 (state->adjustment_open ? 0x10U : 0U);
    payload[2] = state->adjustment_target;
    payload[3] = 0U;
    write_u16_le(&payload[4], state->tank_psi);
    write_u16_le(&payload[6], state->front_left_psi);
    write_u16_le(&payload[8], state->front_right_psi);
    write_u16_le(&payload[10], state->rear_left_psi);
    write_u16_le(&payload[12], state->rear_right_psi);
    return CLUSTER_PROTOCOL_OK;
}

cluster_protocol_result_t cluster_air_ride_state_decode(
    const uint8_t *payload,
    size_t payload_size,
    cluster_air_ride_state_t *state)
{
    if (payload == NULL || state == NULL) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }
    if (payload_size != CLUSTER_AIR_RIDE_STATE_PAYLOAD_SIZE) {
        return CLUSTER_PROTOCOL_BAD_LENGTH;
    }
    if (payload[0] != CLUSTER_AIR_RIDE_PAYLOAD_VERSION) {
        return CLUSTER_PROTOCOL_BAD_VERSION;
    }

    const uint8_t flags = payload[1];
    state->connected = (flags & 0x01U) != 0U;
    state->pressure_valid = (flags & 0x02U) != 0U;
    state->compressor_valid = (flags & 0x04U) != 0U;
    state->compressor_on = (flags & 0x08U) != 0U;
    state->adjustment_open = (flags & 0x10U) != 0U;
    state->adjustment_target = payload[2];
    state->tank_psi = read_u16_le(&payload[4]);
    state->front_left_psi = read_u16_le(&payload[6]);
    state->front_right_psi = read_u16_le(&payload[8]);
    state->rear_left_psi = read_u16_le(&payload[10]);
    state->rear_right_psi = read_u16_le(&payload[12]);
    return CLUSTER_PROTOCOL_OK;
}

cluster_protocol_result_t cluster_air_ride_command_encode(
    const cluster_air_ride_command_t *command,
    uint8_t *payload,
    size_t payload_capacity)
{
    if (command == NULL || payload == NULL) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }
    if (payload_capacity < CLUSTER_AIR_RIDE_COMMAND_PAYLOAD_SIZE) {
        return CLUSTER_PROTOCOL_BUFFER_TOO_SMALL;
    }
    if (command->code > CLUSTER_AIR_RIDE_REAR_DOWN) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }

    payload[0] = CLUSTER_AIR_RIDE_PAYLOAD_VERSION;
    payload[1] = (uint8_t)command->code;
    write_u16_le(&payload[2], command->compressor_on_psi);
    write_u16_le(&payload[4], command->compressor_off_psi);
    return CLUSTER_PROTOCOL_OK;
}

cluster_protocol_result_t cluster_air_ride_command_decode(
    const uint8_t *payload,
    size_t payload_size,
    cluster_air_ride_command_t *command)
{
    if (payload == NULL || command == NULL) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }
    if (payload_size != CLUSTER_AIR_RIDE_COMMAND_PAYLOAD_SIZE) {
        return CLUSTER_PROTOCOL_BAD_LENGTH;
    }
    if (payload[0] != CLUSTER_AIR_RIDE_PAYLOAD_VERSION) {
        return CLUSTER_PROTOCOL_BAD_VERSION;
    }
    if (payload[1] > (uint8_t)CLUSTER_AIR_RIDE_REAR_DOWN) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }

    command->code = (cluster_air_ride_command_code_t)payload[1];
    command->compressor_on_psi = read_u16_le(&payload[2]);
    command->compressor_off_psi = read_u16_le(&payload[4]);
    return CLUSTER_PROTOCOL_OK;
}

cluster_protocol_result_t cluster_vehicle_fast_encode(
    const cluster_vehicle_fast_t *vehicle,
    uint8_t *payload,
    size_t payload_capacity)
{
    if (vehicle == NULL || payload == NULL) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }
    if (payload_capacity < CLUSTER_VEHICLE_FAST_PAYLOAD_SIZE) {
        return CLUSTER_PROTOCOL_BUFFER_TOO_SMALL;
    }

    payload[0] = CLUSTER_VEHICLE_FAST_PAYLOAD_VERSION;
    payload[1] = 0U;
    write_u32_le(&payload[2], vehicle->sample_time_ms);
    write_u16_le(&payload[6], vehicle->rpm);
    write_u16_le(&payload[8], vehicle->speed_tenths_kph);
    write_u16_le(&payload[10], (uint16_t)vehicle->boost_tenths_kpa);
    write_u16_le(&payload[12], (uint16_t)vehicle->coolant_tenths_c);
    write_u16_le(&payload[14], (uint16_t)vehicle->intake_tenths_c);
    write_u16_le(&payload[16], vehicle->afr_thousandths);
    write_u16_le(&payload[18], vehicle->oil_pressure_tenths_kpa);
    write_u16_le(&payload[20], vehicle->fuel_pressure_tenths_kpa);
    write_u16_le(&payload[22], vehicle->battery_mv);
    write_u16_le(&payload[24], vehicle->throttle_tenths_percent);
    write_u32_le(&payload[26], vehicle->valid_channels);
    write_u32_le(&payload[30], vehicle->warning_channels);
    write_u16_le(&payload[34], vehicle->fuel_level_tenths_percent);
    write_u16_le(&payload[36], (uint16_t)vehicle->ignition_deg);
    write_u16_le(&payload[38], vehicle->ethanol_tenths_percent);
    write_u16_le(&payload[40], vehicle->knock_count);
    return CLUSTER_PROTOCOL_OK;
}

cluster_protocol_result_t cluster_vehicle_fast_decode(
    const uint8_t *payload,
    size_t payload_size,
    cluster_vehicle_fast_t *vehicle)
{
    if (payload == NULL || vehicle == NULL) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }
    if (payload_size != CLUSTER_VEHICLE_FAST_PAYLOAD_SIZE) {
        return CLUSTER_PROTOCOL_BAD_LENGTH;
    }
    if (payload[0] != CLUSTER_VEHICLE_FAST_PAYLOAD_VERSION) {
        return CLUSTER_PROTOCOL_BAD_VERSION;
    }

    vehicle->sample_time_ms = read_u32_le(&payload[2]);
    vehicle->rpm = read_u16_le(&payload[6]);
    vehicle->speed_tenths_kph = read_u16_le(&payload[8]);
    vehicle->boost_tenths_kpa = (int16_t)read_u16_le(&payload[10]);
    vehicle->coolant_tenths_c = (int16_t)read_u16_le(&payload[12]);
    vehicle->intake_tenths_c = (int16_t)read_u16_le(&payload[14]);
    vehicle->afr_thousandths = read_u16_le(&payload[16]);
    vehicle->oil_pressure_tenths_kpa = read_u16_le(&payload[18]);
    vehicle->fuel_pressure_tenths_kpa = read_u16_le(&payload[20]);
    vehicle->battery_mv = read_u16_le(&payload[22]);
    vehicle->throttle_tenths_percent = read_u16_le(&payload[24]);
    vehicle->valid_channels = read_u32_le(&payload[26]);
    vehicle->warning_channels = read_u32_le(&payload[30]);
    vehicle->fuel_level_tenths_percent = read_u16_le(&payload[34]);
    vehicle->ignition_deg = (int16_t)read_u16_le(&payload[36]);
    vehicle->ethanol_tenths_percent = read_u16_le(&payload[38]);
    vehicle->knock_count = read_u16_le(&payload[40]);
    return CLUSTER_PROTOCOL_OK;
}

cluster_protocol_result_t cluster_left_settings_encode(
    const cluster_left_settings_t *settings,
    uint8_t *payload,
    size_t payload_capacity)
{
    if (settings == NULL || payload == NULL) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }
    if (payload_capacity < CLUSTER_LEFT_SETTINGS_PAYLOAD_SIZE) {
        return CLUSTER_PROTOCOL_BUFFER_TOO_SMALL;
    }

    payload[0] = CLUSTER_LEFT_SETTINGS_PAYLOAD_VERSION;
    payload[1] = (settings->temperature_celsius ? 0x01U : 0U) |
                 (settings->pressure_kpa ? 0x02U : 0U);
    write_u16_le(&payload[2], settings->revision);
    payload[4] = settings->brightness;
    payload[5] = settings->right_brightness;
    write_u32_le(&payload[6], settings->caution_rgb & 0x00FFFFFFU);
    write_u32_le(&payload[10], settings->critical_rgb & 0x00FFFFFFU);
    write_u32_le(&payload[14], settings->offline_rgb & 0x00FFFFFFU);
    write_u16_le(&payload[18], (uint16_t)settings->ect_caution);
    write_u16_le(&payload[20], (uint16_t)settings->ect_critical);
    write_u16_le(&payload[22], (uint16_t)settings->iat_caution);
    write_u16_le(&payload[24], (uint16_t)settings->iat_critical);
    write_u16_le(&payload[26], (uint16_t)settings->map_caution);
    write_u16_le(&payload[28], (uint16_t)settings->map_critical);
    write_u16_le(&payload[30], (uint16_t)settings->battery_caution);
    write_u16_le(&payload[32], (uint16_t)settings->battery_critical);
    memcpy(&payload[34], settings->value_channels,
           sizeof(settings->value_channels));
    write_u32_le(&payload[37], settings->tach_rgb & 0x00FFFFFFU);
    payload[41] = settings->theme;
    memcpy(&payload[42], settings->right_value_channels,
           sizeof(settings->right_value_channels));
    memcpy(&payload[45], settings->left_gauge_channels,
           sizeof(settings->left_gauge_channels));
    memcpy(&payload[51], settings->right_gauge_channels,
           sizeof(settings->right_gauge_channels));
    memcpy(&payload[57], settings->left_trend_channels,
           sizeof(settings->left_trend_channels));
    memcpy(&payload[59], settings->right_trend_channels,
           sizeof(settings->right_trend_channels));
    return CLUSTER_PROTOCOL_OK;
}

cluster_protocol_result_t cluster_left_settings_decode(
    const uint8_t *payload,
    size_t payload_size,
    cluster_left_settings_t *settings)
{
    if (payload == NULL || settings == NULL) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }
    if (payload_size != CLUSTER_LEFT_SETTINGS_PAYLOAD_SIZE) {
        return CLUSTER_PROTOCOL_BAD_LENGTH;
    }
    if (payload[0] != CLUSTER_LEFT_SETTINGS_PAYLOAD_VERSION) {
        return CLUSTER_PROTOCOL_BAD_VERSION;
    }

    settings->temperature_celsius = (payload[1] & 0x01U) != 0U;
    settings->pressure_kpa = (payload[1] & 0x02U) != 0U;
    settings->revision = read_u16_le(&payload[2]);
    settings->brightness = payload[4];
    settings->right_brightness = payload[5];
    settings->caution_rgb = read_u32_le(&payload[6]) & 0x00FFFFFFU;
    settings->critical_rgb = read_u32_le(&payload[10]) & 0x00FFFFFFU;
    settings->offline_rgb = read_u32_le(&payload[14]) & 0x00FFFFFFU;
    settings->ect_caution = (int16_t)read_u16_le(&payload[18]);
    settings->ect_critical = (int16_t)read_u16_le(&payload[20]);
    settings->iat_caution = (int16_t)read_u16_le(&payload[22]);
    settings->iat_critical = (int16_t)read_u16_le(&payload[24]);
    settings->map_caution = (int16_t)read_u16_le(&payload[26]);
    settings->map_critical = (int16_t)read_u16_le(&payload[28]);
    settings->battery_caution = (int16_t)read_u16_le(&payload[30]);
    settings->battery_critical = (int16_t)read_u16_le(&payload[32]);
    memcpy(settings->value_channels, &payload[34],
           sizeof(settings->value_channels));
    settings->tach_rgb = read_u32_le(&payload[37]) & 0x00FFFFFFU;
    settings->theme = payload[41];
    memcpy(settings->right_value_channels, &payload[42],
           sizeof(settings->right_value_channels));
    memcpy(settings->left_gauge_channels, &payload[45],
           sizeof(settings->left_gauge_channels));
    memcpy(settings->right_gauge_channels, &payload[51],
           sizeof(settings->right_gauge_channels));
    memcpy(settings->left_trend_channels, &payload[57],
           sizeof(settings->left_trend_channels));
    memcpy(settings->right_trend_channels, &payload[59],
           sizeof(settings->right_trend_channels));
    return CLUSTER_PROTOCOL_OK;
}

cluster_protocol_result_t cluster_knob_command_encode(
    const cluster_knob_command_t *command,
    uint8_t *payload,
    size_t payload_capacity)
{
    if (command == NULL || payload == NULL) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }
    if (payload_capacity < CLUSTER_KNOB_COMMAND_PAYLOAD_SIZE) {
        return CLUSTER_PROTOCOL_BUFFER_TOO_SMALL;
    }
    if (!knob_command_valid((uint8_t)command->code, command->flags)) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }

    payload[0] = CLUSTER_KNOB_COMMAND_PAYLOAD_VERSION;
    payload[1] = (uint8_t)command->code;
    payload[2] = command->target;
    payload[3] = command->flags;
    write_u32_le(&payload[4], (uint32_t)command->value);
    return CLUSTER_PROTOCOL_OK;
}

cluster_protocol_result_t cluster_knob_command_decode(
    const uint8_t *payload,
    size_t payload_size,
    cluster_knob_command_t *command)
{
    if (payload == NULL || command == NULL) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }
    if (payload_size != CLUSTER_KNOB_COMMAND_PAYLOAD_SIZE) {
        return CLUSTER_PROTOCOL_BAD_LENGTH;
    }
    if (payload[0] != CLUSTER_KNOB_COMMAND_PAYLOAD_VERSION) {
        return CLUSTER_PROTOCOL_BAD_VERSION;
    }
    if (!knob_command_valid(payload[1], payload[3])) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }

    command->code = (cluster_knob_command_code_t)payload[1];
    command->target = payload[2];
    command->flags = payload[3];
    command->value = (int32_t)read_u32_le(&payload[4]);
    return CLUSTER_PROTOCOL_OK;
}

cluster_protocol_result_t cluster_display_mode_encode(
    const cluster_display_mode_t *mode,
    uint8_t *payload,
    size_t payload_capacity)
{
    if (!mode || !payload || mode->target >= CLUSTER_DISPLAY_COUNT ||
        mode->view >= CLUSTER_VIEW_COUNT) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }
    if (payload_capacity < CLUSTER_DISPLAY_MODE_PAYLOAD_SIZE) {
        return CLUSTER_PROTOCOL_BUFFER_TOO_SMALL;
    }
    payload[0] = CLUSTER_DISPLAY_MODE_PAYLOAD_VERSION;
    payload[1] = (uint8_t)mode->target;
    payload[2] = (uint8_t)mode->view;
    payload[3] = 0U;
    return CLUSTER_PROTOCOL_OK;
}

cluster_protocol_result_t cluster_display_mode_decode(
    const uint8_t *payload,
    size_t payload_size,
    cluster_display_mode_t *mode)
{
    if (!payload || !mode) return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    if (payload_size != CLUSTER_DISPLAY_MODE_PAYLOAD_SIZE) {
        return CLUSTER_PROTOCOL_BAD_LENGTH;
    }
    if (payload[0] != CLUSTER_DISPLAY_MODE_PAYLOAD_VERSION) {
        return CLUSTER_PROTOCOL_BAD_VERSION;
    }
    if (payload[1] >= CLUSTER_DISPLAY_COUNT ||
        payload[2] >= CLUSTER_VIEW_COUNT || payload[3] != 0U) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }
    mode->target = (cluster_display_target_t)payload[1];
    mode->view = (cluster_display_view_t)payload[2];
    return CLUSTER_PROTOCOL_OK;
}

cluster_protocol_result_t cluster_logging_status_encode(
    const cluster_logging_status_t *status,
    uint8_t *payload,
    size_t payload_capacity)
{
    if (!status || !payload) return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    if (payload_capacity < CLUSTER_LOGGING_STATUS_PAYLOAD_SIZE) {
        return CLUSTER_PROTOCOL_BUFFER_TOO_SMALL;
    }
    payload[0] = CLUSTER_LOGGING_STATUS_PAYLOAD_VERSION;
    payload[1] = (status->available ? 0x01U : 0U) |
                 (status->recording ? 0x02U : 0U);
    write_u16_le(&payload[2], status->session_index);
    write_u32_le(&payload[4], status->record_count);
    write_u32_le(&payload[8], status->dropped_count);
    return CLUSTER_PROTOCOL_OK;
}

cluster_protocol_result_t cluster_logging_status_decode(
    const uint8_t *payload,
    size_t payload_size,
    cluster_logging_status_t *status)
{
    if (!payload || !status) return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    if (payload_size != CLUSTER_LOGGING_STATUS_PAYLOAD_SIZE) {
        return CLUSTER_PROTOCOL_BAD_LENGTH;
    }
    if (payload[0] != CLUSTER_LOGGING_STATUS_PAYLOAD_VERSION) {
        return CLUSTER_PROTOCOL_BAD_VERSION;
    }
    if ((payload[1] & ~0x03U) != 0U) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }
    status->available = (payload[1] & 0x01U) != 0U;
    status->recording = (payload[1] & 0x02U) != 0U;
    status->session_index = read_u16_le(&payload[2]);
    status->record_count = read_u32_le(&payload[4]);
    status->dropped_count = read_u32_le(&payload[8]);
    return CLUSTER_PROTOCOL_OK;
}

bool cluster_protocol_self_test(void)
{
    const uint8_t payload[] = {0x50, 0x34};
    uint8_t frame[CLUSTER_PROTOCOL_MAX_FRAME_SIZE];
    size_t frame_size = 0U;
    cluster_frame_view_t view = {0};
    return cluster_frame_encode(CLUSTER_MSG_HEARTBEAT, 1U, 0U, payload,
                                sizeof(payload), frame, sizeof(frame),
                                &frame_size) == CLUSTER_PROTOCOL_OK &&
           cluster_frame_decode(frame, frame_size, &view) ==
               CLUSTER_PROTOCOL_OK &&
           view.type == CLUSTER_MSG_HEARTBEAT && view.sequence == 1U &&
           view.payload_size == sizeof(payload) &&
           memcmp(view.payload, payload, sizeof(payload)) == 0;
}

static bool climate_positions_valid(uint8_t fan, uint8_t mode, uint8_t temp)
{
    return fan <= CLUSTER_CLIMATE_FAN_MAX && mode <= CLUSTER_CLIMATE_MODE_MAX &&
           temp <= CLUSTER_CLIMATE_TEMP_MAX;
}

cluster_protocol_result_t cluster_climate_command_encode(
    const cluster_climate_command_t *command,
    uint8_t *payload,
    size_t payload_capacity)
{
    if (!command || !payload ||
        !climate_positions_valid(command->fan, command->mode, command->temp)) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }
    if (payload_capacity < CLUSTER_CLIMATE_COMMAND_PAYLOAD_SIZE) {
        return CLUSTER_PROTOCOL_BUFFER_TOO_SMALL;
    }
    payload[0] = CLUSTER_CLIMATE_PAYLOAD_VERSION;
    payload[1] = command->fan;
    payload[2] = command->mode;
    payload[3] = command->temp;
    return CLUSTER_PROTOCOL_OK;
}

cluster_protocol_result_t cluster_climate_command_decode(
    const uint8_t *payload,
    size_t payload_size,
    cluster_climate_command_t *command)
{
    if (!payload || !command) return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    if (payload_size != CLUSTER_CLIMATE_COMMAND_PAYLOAD_SIZE) {
        return CLUSTER_PROTOCOL_BAD_LENGTH;
    }
    if (payload[0] != CLUSTER_CLIMATE_PAYLOAD_VERSION) {
        return CLUSTER_PROTOCOL_BAD_VERSION;
    }
    if (!climate_positions_valid(payload[1], payload[2], payload[3])) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }
    command->fan = payload[1];
    command->mode = payload[2];
    command->temp = payload[3];
    return CLUSTER_PROTOCOL_OK;
}

cluster_protocol_result_t cluster_climate_state_encode(
    const cluster_climate_state_t *state,
    uint8_t *payload,
    size_t payload_capacity)
{
    if (!state || !payload) return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    if (payload_capacity < CLUSTER_CLIMATE_STATE_PAYLOAD_SIZE) {
        return CLUSTER_PROTOCOL_BUFFER_TOO_SMALL;
    }
    payload[0] = CLUSTER_CLIMATE_PAYLOAD_VERSION;
    payload[1] = state->connected ? 0x01U : 0U;
    payload[2] = state->fan;
    payload[3] = state->mode;
    payload[4] = state->temp;
    payload[5] = state->target_fan;
    payload[6] = state->target_mode;
    payload[7] = state->target_temp;
    payload[8] = state->moving_mask;
    payload[9] = state->fault_flags;
    return CLUSTER_PROTOCOL_OK;
}

cluster_protocol_result_t cluster_climate_state_decode(
    const uint8_t *payload,
    size_t payload_size,
    cluster_climate_state_t *state)
{
    if (!payload || !state) return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    if (payload_size != CLUSTER_CLIMATE_STATE_PAYLOAD_SIZE) {
        return CLUSTER_PROTOCOL_BAD_LENGTH;
    }
    if (payload[0] != CLUSTER_CLIMATE_PAYLOAD_VERSION) {
        return CLUSTER_PROTOCOL_BAD_VERSION;
    }
    if ((payload[1] & ~0x01U) != 0U ||
        !climate_positions_valid(payload[2], payload[3], payload[4]) ||
        !climate_positions_valid(payload[5], payload[6], payload[7])) {
        return CLUSTER_PROTOCOL_INVALID_ARGUMENT;
    }
    state->connected = (payload[1] & 0x01U) != 0U;
    state->fan = payload[2];
    state->mode = payload[3];
    state->temp = payload[4];
    state->target_fan = payload[5];
    state->target_mode = payload[6];
    state->target_temp = payload[7];
    state->moving_mask = payload[8];
    state->fault_flags = payload[9];
    return CLUSTER_PROTOCOL_OK;
}

cluster_air_ride_command_code_t cluster_air_target_command(uint8_t target,
                                                           bool raise)
{
    static const cluster_air_ride_command_code_t
        commands[CLUSTER_AIR_TARGET_COUNT][2] = {
        [CLUSTER_AIR_TARGET_ALL] = {CLUSTER_AIR_RIDE_ALL_DOWN,
                                    CLUSTER_AIR_RIDE_ALL_UP},
        [CLUSTER_AIR_TARGET_FRONT] = {CLUSTER_AIR_RIDE_FRONT_DOWN,
                                      CLUSTER_AIR_RIDE_FRONT_UP},
        [CLUSTER_AIR_TARGET_REAR] = {CLUSTER_AIR_RIDE_REAR_DOWN,
                                     CLUSTER_AIR_RIDE_REAR_UP},
        [CLUSTER_AIR_TARGET_FRONT_LEFT] = {CLUSTER_AIR_RIDE_FRONT_LEFT_DOWN,
                                           CLUSTER_AIR_RIDE_FRONT_LEFT_UP},
        [CLUSTER_AIR_TARGET_FRONT_RIGHT] = {CLUSTER_AIR_RIDE_FRONT_RIGHT_DOWN,
                                            CLUSTER_AIR_RIDE_FRONT_RIGHT_UP},
        [CLUSTER_AIR_TARGET_REAR_LEFT] = {CLUSTER_AIR_RIDE_REAR_LEFT_DOWN,
                                          CLUSTER_AIR_RIDE_REAR_LEFT_UP},
        [CLUSTER_AIR_TARGET_REAR_RIGHT] = {CLUSTER_AIR_RIDE_REAR_RIGHT_DOWN,
                                           CLUSTER_AIR_RIDE_REAR_RIGHT_UP},
    };
    if (target >= CLUSTER_AIR_TARGET_COUNT) return CLUSTER_AIR_RIDE_RELEASE;
    return commands[target][raise ? 1 : 0];
}
