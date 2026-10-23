#include <stdio.h>

#include "side_pages_internal.h"
#include "vehicle_data.h"

#define COLOR_WHITE 0xECECE7U

int32_t side_tenths_c_to_tenths_f(int16_t tenths_c)
{
    return ((int32_t)tenths_c * 9) / 5 + 320;
}

static uint32_t tenths_kph_to_mph(uint16_t speed_tenths_kph)
{
    return (uint32_t)(((uint64_t)speed_tenths_kph * 621371U + 5000000U) /
                      10000000U);
}

/* Boost reads in PSI, vacuum in inHg, both to one decimal. */
static int32_t boost_display_tenths(int16_t gauge_tenths_kpa)
{
    return gauge_tenths_kpa < 0
        ? ((int32_t)gauge_tenths_kpa * 2953 - 5000) / 10000
        : ((int32_t)gauge_tenths_kpa * 14504 + 50000) / 100000;
}

const char *side_channel_name(uint8_t channel)
{
    static const char *const names[VEHICLE_DATA_COUNT] = {
        [VEHICLE_DATA_RPM] = "RPM",
        [VEHICLE_DATA_SPEED] = "SPEED",
        [VEHICLE_DATA_BOOST] = "BOOST",
        [VEHICLE_DATA_COOLANT_TEMP] = "COOLANT",
        [VEHICLE_DATA_INTAKE_TEMP] = "INTAKE",
        [VEHICLE_DATA_AFR] = "AFR",
        [VEHICLE_DATA_OIL_PRESSURE] = "OIL PRESS",
        [VEHICLE_DATA_FUEL_PRESSURE] = "FUEL PRESS",
        [VEHICLE_DATA_BATTERY] = "BATTERY",
        [VEHICLE_DATA_THROTTLE] = "THROTTLE",
        [VEHICLE_DATA_FUEL_LEVEL] = "FUEL",
        [VEHICLE_DATA_IGNITION] = "IGNITION",
        [VEHICLE_DATA_ETHANOL] = "ETHANOL",
        [VEHICLE_DATA_KNOCK] = "KNOCK",
    };
    return channel < VEHICLE_DATA_COUNT ? names[channel] : "--";
}

const char *side_channel_unit(const cluster_vehicle_fast_t *vehicle,
                              uint8_t channel)
{
    switch ((vehicle_data_channel_t)channel) {
    case VEHICLE_DATA_RPM: return "RPM";
    case VEHICLE_DATA_SPEED: return "MPH";
    case VEHICLE_DATA_BOOST:
        return vehicle_data_channel_valid(vehicle->valid_channels, channel) &&
                       vehicle->boost_tenths_kpa < 0
                   ? "INHG" : "PSI";
    case VEHICLE_DATA_COOLANT_TEMP:
    case VEHICLE_DATA_INTAKE_TEMP: return "F";
    case VEHICLE_DATA_AFR: return "AFR";
    case VEHICLE_DATA_OIL_PRESSURE:
    case VEHICLE_DATA_FUEL_PRESSURE: return "PSI";
    case VEHICLE_DATA_BATTERY: return "VOLTS";
    case VEHICLE_DATA_THROTTLE:
    case VEHICLE_DATA_FUEL_LEVEL:
    case VEHICLE_DATA_ETHANOL: return "%";
    case VEHICLE_DATA_IGNITION: return "DEG";
    case VEHICLE_DATA_KNOCK: return "COUNT";
    default: return "";
    }
}

void side_channel_format(char *output, size_t output_size,
                         const cluster_vehicle_fast_t *vehicle,
                         uint8_t channel)
{
    switch ((vehicle_data_channel_t)channel) {
    case VEHICLE_DATA_RPM:
        snprintf(output, output_size, "%u", vehicle->rpm);
        break;
    case VEHICLE_DATA_SPEED:
        snprintf(output, output_size, "%lu",
                 (unsigned long)tenths_kph_to_mph(vehicle->speed_tenths_kph));
        break;
    case VEHICLE_DATA_BOOST: {
        const int32_t tenths = boost_display_tenths(vehicle->boost_tenths_kpa);
        const int32_t magnitude = tenths < 0 ? -tenths : tenths;
        snprintf(output, output_size, "%s%ld.%ld", tenths < 0 ? "-" : "",
                 (long)(magnitude / 10), (long)(magnitude % 10));
        break;
    }
    case VEHICLE_DATA_COOLANT_TEMP:
        snprintf(output, output_size, "%ld",
                 (long)(side_tenths_c_to_tenths_f(vehicle->coolant_tenths_c) /
                        10));
        break;
    case VEHICLE_DATA_INTAKE_TEMP:
        snprintf(output, output_size, "%ld",
                 (long)(side_tenths_c_to_tenths_f(vehicle->intake_tenths_c) /
                        10));
        break;
    case VEHICLE_DATA_AFR:
        snprintf(output, output_size, "%u.%u", vehicle->afr_thousandths / 1000U,
                 (vehicle->afr_thousandths % 1000U) / 100U);
        break;
    case VEHICLE_DATA_OIL_PRESSURE:
        snprintf(output, output_size, "%lu", (unsigned long)
                 (((uint32_t)vehicle->oil_pressure_tenths_kpa * 14504U) /
                  1000000U));
        break;
    case VEHICLE_DATA_FUEL_PRESSURE:
        snprintf(output, output_size, "%lu", (unsigned long)
                 (((uint32_t)vehicle->fuel_pressure_tenths_kpa * 14504U) /
                  1000000U));
        break;
    case VEHICLE_DATA_BATTERY:
        snprintf(output, output_size, "%u.%u", vehicle->battery_mv / 1000U,
                 (vehicle->battery_mv % 1000U) / 100U);
        break;
    case VEHICLE_DATA_THROTTLE:
        snprintf(output, output_size, "%u", vehicle->throttle_tenths_percent / 10U);
        break;
    case VEHICLE_DATA_FUEL_LEVEL:
        snprintf(output, output_size, "%u", vehicle->fuel_level_tenths_percent / 10U);
        break;
    case VEHICLE_DATA_IGNITION:
        snprintf(output, output_size, "%d", vehicle->ignition_deg);
        break;
    case VEHICLE_DATA_ETHANOL:
        snprintf(output, output_size, "%u",
                 (vehicle->ethanol_tenths_percent + 5U) / 10U);
        break;
    case VEHICLE_DATA_KNOCK:
        snprintf(output, output_size, "%u", vehicle->knock_count);
        break;
    default:
        snprintf(output, output_size, "--");
        break;
    }
}

uint32_t side_channel_color(const cluster_vehicle_fast_t *vehicle,
                            uint8_t channel,
                            const cluster_left_settings_t *settings)
{
    int32_t value;
    int32_t caution;
    int32_t critical;
    bool lower_is_worse = false;

    switch ((vehicle_data_channel_t)channel) {
    case VEHICLE_DATA_BOOST:
        value = ((int32_t)vehicle->boost_tenths_kpa * 14504) / 100000;
        caution = settings->map_caution;
        critical = settings->map_critical;
        break;
    case VEHICLE_DATA_COOLANT_TEMP:
        value = side_tenths_c_to_tenths_f(vehicle->coolant_tenths_c);
        caution = settings->ect_caution;
        critical = settings->ect_critical;
        break;
    case VEHICLE_DATA_INTAKE_TEMP:
        value = side_tenths_c_to_tenths_f(vehicle->intake_tenths_c);
        caution = settings->iat_caution;
        critical = settings->iat_critical;
        break;
    case VEHICLE_DATA_BATTERY:
        value = (vehicle->battery_mv + 50U) / 100U;
        caution = settings->battery_caution;
        critical = settings->battery_critical;
        lower_is_worse = true;
        break;
    default:
        return COLOR_WHITE;
    }

    if (lower_is_worse) {
        if (value <= critical) return settings->critical_rgb;
        if (value <= caution) return settings->caution_rgb;
    } else {
        if (value >= critical) return settings->critical_rgb;
        if (value >= caution) return settings->caution_rgb;
    }
    return COLOR_WHITE;
}
