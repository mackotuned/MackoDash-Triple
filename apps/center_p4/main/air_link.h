#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cluster_protocol.h"
#include "esp_err.h"
#include "settings_service.h"

enum {
    AIR_LINK_FRONT_LEFT,
    AIR_LINK_FRONT_RIGHT,
    AIR_LINK_REAR_LEFT,
    AIR_LINK_REAR_RIGHT,
    AIR_LINK_TANK,
    AIR_LINK_PRESSURE_COUNT,
};

/* Latest Air Ride state reported by the right display. */
typedef struct {
    bool fresh; /* a report arrived within the last 500 ms */
    bool connected;
    bool pressure_valid;
    bool compressor_valid;
    bool compressor_on;
    bool adjustment_open;
    uint8_t adjustment_target;
    uint16_t psi[AIR_LINK_PRESSURE_COUNT];
} air_link_state_t;

void air_link_store(const cluster_air_ride_state_t *state);
/* Highlights the knob's target until the next report from the right. */
void air_link_set_target(uint8_t target);
void air_link_get(air_link_state_t *state);

/* Sends a command to the right display, which owns the Air Ride radio. */
esp_err_t air_link_send_command(cluster_air_ride_command_code_t code);

/* Average pressure across the bags a knob target covers. */
uint16_t air_link_target_pressure(const air_link_state_t *state,
                                  uint8_t target);

/* settings_service callbacks for the phone UI. */
bool air_link_web_state(settings_air_state_t *state, void *context);
esp_err_t air_link_web_command(cluster_air_ride_command_code_t command,
                               void *context);
