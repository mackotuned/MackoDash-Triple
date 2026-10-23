#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cluster_protocol.h"
#include "esp_err.h"
#include "settings_schema.h"

typedef void (*settings_changed_cb_t)(const cluster_settings_t *settings,
                                      void *context);

typedef struct {
    bool right_online;
    bool air_connected;
    bool pressure_valid;
    bool compressor_valid;
    bool compressor_on;
    uint16_t tank_psi;
    uint16_t front_left_psi;
    uint16_t front_right_psi;
    uint16_t rear_left_psi;
    uint16_t rear_right_psi;
} settings_air_state_t;

typedef bool (*settings_air_state_cb_t)(settings_air_state_t *state,
                                        void *context);
typedef esp_err_t (*settings_air_command_cb_t)(
    cluster_air_ride_command_code_t command, void *context);

esp_err_t settings_service_start(settings_changed_cb_t callback, void *context,
                                 settings_air_state_cb_t air_state_callback,
                                 settings_air_command_cb_t air_command_callback,
                                 void *air_context);
void settings_service_get(cluster_settings_t *settings);
esp_err_t settings_service_set_all_brightness(uint8_t brightness,
                                              bool persist);
bool settings_service_simulation_enabled(void);
