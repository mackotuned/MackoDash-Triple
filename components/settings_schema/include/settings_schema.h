#pragma once

#include <stdint.h>

#include "vehicle_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLUSTER_SETTINGS_SCHEMA_VERSION 1U

typedef enum {
    CLUSTER_SCREEN_CENTER = 0,
    CLUSTER_SCREEN_LEFT,
    CLUSTER_SCREEN_RIGHT,
    CLUSTER_SCREEN_COUNT,
} cluster_screen_role_t;

typedef enum {
    CLUSTER_GAUGE_STYLE_ANALOG = 0,
    CLUSTER_GAUGE_STYLE_DIGITAL,
    CLUSTER_GAUGE_STYLE_BAR,
} cluster_gauge_style_t;

typedef struct {
    vehicle_data_channel_t primary_channel;
    vehicle_data_channel_t secondary_channel;
    cluster_gauge_style_t style;
    uint16_t minimum;
    uint16_t maximum;
    uint16_t warning_threshold;
} cluster_screen_assignment_t;

typedef struct {
    uint16_t schema_version;
    uint16_t revision;
    cluster_screen_assignment_t screens[CLUSTER_SCREEN_COUNT];
    uint8_t brightness[CLUSTER_SCREEN_COUNT];
    uint8_t units_profile;
} cluster_settings_t;

#ifdef __cplusplus
}
#endif
