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

bool side_channel_value_tenths(const cluster_vehicle_fast_t *vehicle,
                               uint8_t channel, int32_t *value)
{
    if (!vehicle_data_channel_valid(vehicle->valid_channels, channel)) {
        return false;
    }
    switch ((vehicle_data_channel_t)channel) {
    case VEHICLE_DATA_RPM: *value = (int32_t)vehicle->rpm * 10; break;
    case VEHICLE_DATA_SPEED:
        *value = (int32_t)(((uint64_t)vehicle->speed_tenths_kph * 621371U +
                            500000U) / 1000000U);
        break;
    case VEHICLE_DATA_BOOST:
        /* Signed PSI: vacuum plots below zero. */
        *value = ((int32_t)vehicle->boost_tenths_kpa * 14504) / 100000;
        break;
    case VEHICLE_DATA_COOLANT_TEMP:
        *value = side_tenths_c_to_tenths_f(vehicle->coolant_tenths_c);
        break;
    case VEHICLE_DATA_INTAKE_TEMP:
        *value = side_tenths_c_to_tenths_f(vehicle->intake_tenths_c);
        break;
    case VEHICLE_DATA_AFR: *value = vehicle->afr_thousandths / 100; break;
    case VEHICLE_DATA_OIL_PRESSURE:
        *value = ((int32_t)vehicle->oil_pressure_tenths_kpa * 14504) / 100000;
        break;
    case VEHICLE_DATA_FUEL_PRESSURE:
        *value = ((int32_t)vehicle->fuel_pressure_tenths_kpa * 14504) / 100000;
        break;
    case VEHICLE_DATA_BATTERY: *value = vehicle->battery_mv / 100; break;
    case VEHICLE_DATA_THROTTLE: *value = vehicle->throttle_tenths_percent; break;
    case VEHICLE_DATA_FUEL_LEVEL:
        *value = vehicle->fuel_level_tenths_percent;
        break;
    case VEHICLE_DATA_IGNITION: *value = (int32_t)vehicle->ignition_deg * 10; break;
    case VEHICLE_DATA_ETHANOL: *value = vehicle->ethanol_tenths_percent; break;
    case VEHICLE_DATA_KNOCK: *value = (int32_t)vehicle->knock_count * 10; break;
    default: return false;
    }
    return true;
}

void side_channel_range_tenths(uint8_t channel, int32_t *minimum,
                               int32_t *maximum)
{
    static const int16_t ranges[VEHICLE_DATA_COUNT][2] = {
        [VEHICLE_DATA_RPM] = {0, 9000},       /* x10 below */
        [VEHICLE_DATA_SPEED] = {0, 1600},
        [VEHICLE_DATA_BOOST] = {-150, 300},
        [VEHICLE_DATA_COOLANT_TEMP] = {1000, 2600},
        [VEHICLE_DATA_INTAKE_TEMP] = {400, 2000},
        [VEHICLE_DATA_AFR] = {100, 200},
        [VEHICLE_DATA_OIL_PRESSURE] = {0, 1000},
        [VEHICLE_DATA_FUEL_PRESSURE] = {0, 800},
        [VEHICLE_DATA_BATTERY] = {100, 160},
        [VEHICLE_DATA_THROTTLE] = {0, 1000},
        [VEHICLE_DATA_FUEL_LEVEL] = {0, 1000},
        [VEHICLE_DATA_IGNITION] = {-100, 500},
        [VEHICLE_DATA_ETHANOL] = {0, 1000},
        [VEHICLE_DATA_KNOCK] = {0, 200},
    };
    if (channel >= VEHICLE_DATA_COUNT) {
        *minimum = 0;
        *maximum = 1000;
        return;
    }
    const int32_t scale = channel == VEHICLE_DATA_RPM ? 10 : 1;
    *minimum = ranges[channel][0] * scale;
    *maximum = ranges[channel][1] * scale;
}

const side_palette_t *side_palette(bool graphite)
{
    static const side_palette_t classic = {
        .background = 0x000000, .panel = 0x0C0C0D, .border = 0x2C2C2F,
        .text = 0xECECE7, .dim = 0x9A9A96, .accent = 0xE81E1E,
        .accent2 = 0xEC5C28, .grid = 0x19191B,
    };
    static const side_palette_t graphite_palette = {
        .background = 0x0B0E11, .panel = 0x12171C, .border = 0x26323C,
        .text = 0xEEF0F2, .dim = 0x8A97A3, .accent = 0x5EC8F2,
        .accent2 = 0xE8EDF2, .grid = 0x18212A,
    };
    return graphite ? &graphite_palette : &classic;
}

lv_obj_t *side_offline_ring_create(lv_obj_t *screen)
{
    lv_obj_t *ring = lv_obj_create(screen);
    lv_obj_set_size(ring, 472, 472);
    lv_obj_center(ring);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ring, 6, LV_PART_MAIN);
    lv_obj_set_style_border_color(ring, lv_color_hex(0x84868DU), LV_PART_MAIN);
    lv_obj_set_clickable(ring, false);
    return ring;
}

lv_obj_t *side_panel_create(lv_obj_t *parent, int32_t x, int32_t y,
                            int32_t width, int32_t height)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_radius(panel, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(panel, false);
    lv_obj_set_clickable(panel, false);
    return panel;
}

void side_panel_style(lv_obj_t *panel, const side_palette_t *palette)
{
    lv_obj_set_style_bg_color(panel, lv_color_hex(palette->panel), LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_hex(palette->border),
                                  LV_PART_MAIN);
}
