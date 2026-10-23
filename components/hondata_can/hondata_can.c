#include "hondata_can.h"

#include <limits.h>
#include <string.h>

static uint16_t read_u16_be(const uint8_t *data)
{
    return ((uint16_t)data[0] << 8U) | data[1];
}

static int16_t clamp_i32_to_i16(int32_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

static uint16_t clamp_u32_to_u16(uint32_t value)
{
    return value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
}

void hondata_state_reset(hondata_state_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

hondata_decode_result_t hondata_decode_frame(
    hondata_state_t *state,
    uint32_t identifier,
    const uint8_t *data,
    size_t data_length,
    uint32_t sample_time_ms)
{
    static const uint8_t minimum_lengths[HONDATA_CAN_FRAME_COUNT] = {
        6, 4, 4, 4, 4, 2, 4, 8, 8, 3,
    };

    if (state == NULL || data == NULL) {
        return HONDATA_DECODE_INVALID_ARGUMENT;
    }
    if (identifier < HONDATA_CAN_ID_FIRST || identifier > HONDATA_CAN_ID_LAST) {
        return HONDATA_DECODE_NOT_HONDATA;
    }

    const uint32_t frame_index = identifier - HONDATA_CAN_ID_FIRST;
    if (data_length < minimum_lengths[frame_index]) {
        return HONDATA_DECODE_INVALID_LENGTH;
    }

    switch (identifier) {
        case 0x660:
            state->rpm = read_u16_be(&data[0]);
            state->speed_tenths_kph =
                clamp_u32_to_u16((uint32_t)read_u16_be(&data[2]) * 10U);
            state->gear = data[4];
            state->battery_mv = (uint16_t)data[5] * 100U;
            break;

        case 0x661:
            state->intake_temp_tenths_c =
                clamp_i32_to_i16((int32_t)read_u16_be(&data[0]) * 10);
            state->coolant_temp_tenths_c =
                clamp_i32_to_i16((int32_t)read_u16_be(&data[2]) * 10);
            break;

        case 0x662:
            state->throttle_tenths_percent =
                clamp_u32_to_u16((uint32_t)read_u16_be(&data[0]) * 10U);
            state->map_gauge_tenths_kpa = clamp_i32_to_i16(
                (int32_t)read_u16_be(&data[2]) - 1013);
            break;

        case 0x663:
            state->injector_duration_raw = read_u16_be(&data[0]);
            state->ignition_angle_deg = (int16_t)read_u16_be(&data[2]);
            break;

        case 0x664: {
            const uint32_t raw = read_u16_be(&data[2]);
            state->afr_thousandths =
                clamp_u32_to_u16((raw * 44835U + 50000U) / 100000U);
            break;
        }

        case 0x665:
            state->knock_count = read_u16_be(&data[0]);
            break;

        case 0x666:
            state->cam_angle_target_deg = (int16_t)read_u16_be(&data[0]);
            state->cam_angle_actual_deg = (int16_t)read_u16_be(&data[2]);
            break;

        case 0x667:
        case 0x668: {
            const size_t first_channel = identifier == 0x667 ? 0U : 4U;
            for (size_t index = 0; index < 4U; ++index) {
                const uint32_t raw = read_u16_be(&data[index * 2U]);
                state->analog_mv[first_channel + index] =
                    clamp_u32_to_u16((raw * 122U + 50U) / 100U);
            }
            break;
        }

        case 0x669:
            state->ethanol_frequency_hz = data[0];
            state->ethanol_duty_hundredths_percent = (uint16_t)data[1] * 256U;
            state->ethanol_content_tenths_percent = (uint16_t)data[2] * 10U;
            break;

        default:
            return HONDATA_DECODE_NOT_HONDATA;
    }

    state->received_frame_mask |= 1UL << frame_index;
    state->last_frame_time_ms[frame_index] = sample_time_ms;
    return HONDATA_DECODE_OK;
}

void hondata_apply_vehicle_fast(const hondata_state_t *hondata,
                                vehicle_fast_state_t *vehicle)
{
    if (hondata == NULL || vehicle == NULL) {
        return;
    }

    const uint32_t frame_660 = 1UL << (0x660U - HONDATA_CAN_ID_FIRST);
    const uint32_t frame_661 = 1UL << (0x661U - HONDATA_CAN_ID_FIRST);
    const uint32_t frame_662 = 1UL << (0x662U - HONDATA_CAN_ID_FIRST);
    const uint32_t frame_664 = 1UL << (0x664U - HONDATA_CAN_ID_FIRST);

    if ((hondata->received_frame_mask & frame_660) != 0U) {
        vehicle->rpm = hondata->rpm;
        vehicle->speed_tenths_kph = hondata->speed_tenths_kph;
        vehicle->battery_mv = hondata->battery_mv;
        vehicle->valid_channels |=
            vehicle_data_channel_mask(VEHICLE_DATA_RPM) |
            vehicle_data_channel_mask(VEHICLE_DATA_SPEED) |
            vehicle_data_channel_mask(VEHICLE_DATA_BATTERY);
    }
    if ((hondata->received_frame_mask & frame_661) != 0U) {
        vehicle->coolant_tenths_c = hondata->coolant_temp_tenths_c;
        vehicle->intake_tenths_c = hondata->intake_temp_tenths_c;
        vehicle->valid_channels |=
            vehicle_data_channel_mask(VEHICLE_DATA_COOLANT_TEMP) |
            vehicle_data_channel_mask(VEHICLE_DATA_INTAKE_TEMP);
    }
    if ((hondata->received_frame_mask & frame_662) != 0U) {
        vehicle->boost_tenths_kpa = hondata->map_gauge_tenths_kpa;
        vehicle->throttle_tenths_percent = hondata->throttle_tenths_percent;
        vehicle->valid_channels |=
            vehicle_data_channel_mask(VEHICLE_DATA_BOOST) |
            vehicle_data_channel_mask(VEHICLE_DATA_THROTTLE);
    }
    if ((hondata->received_frame_mask & frame_664) != 0U) {
        vehicle->afr_thousandths = hondata->afr_thousandths;
        vehicle->valid_channels |=
            vehicle_data_channel_mask(VEHICLE_DATA_AFR);
    }
}

void hondata_apply_fuel_level(const hondata_state_t *hondata,
                              uint8_t analog_input,
                              uint16_t empty_mv,
                              uint16_t full_mv,
                              vehicle_fast_state_t *vehicle)
{
    if (hondata == NULL || vehicle == NULL || analog_input >= 8U ||
        empty_mv == full_mv) {
        return;
    }

    const uint32_t frame_id = analog_input < 4U ? 0x667U : 0x668U;
    const uint32_t frame_mask = 1UL << (frame_id - HONDATA_CAN_ID_FIRST);
    if ((hondata->received_frame_mask & frame_mask) == 0U) {
        return;
    }

    const int32_t numerator =
        ((int32_t)hondata->analog_mv[analog_input] - (int32_t)empty_mv) * 1000;
    const int32_t denominator = (int32_t)full_mv - (int32_t)empty_mv;
    int32_t fuel_level = numerator / denominator;
    if (fuel_level < 0) fuel_level = 0;
    if (fuel_level > 1000) fuel_level = 1000;
    vehicle->fuel_level_tenths_percent = (uint16_t)fuel_level;
    vehicle->valid_channels |= vehicle_data_channel_mask(VEHICLE_DATA_FUEL_LEVEL);
}