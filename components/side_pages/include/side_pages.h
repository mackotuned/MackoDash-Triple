#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cluster_protocol.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The five pages every side display offers. Swiping left or right cycles
 * DATA -> GAUGES -> SPEEDOMETER -> AIR RIDE -> CLIMATE; the knob can select
 * any of them. */

enum {
    SIDE_AIR_FRONT_LEFT,
    SIDE_AIR_FRONT_RIGHT,
    SIDE_AIR_REAR_LEFT,
    SIDE_AIR_REAR_RIGHT,
    SIDE_AIR_TANK,
    SIDE_AIR_PRESSURE_COUNT,
};

typedef struct {
    bool connected; /* Air Ride controller reachable */
    bool pressure_valid;
    bool compressor_valid;
    bool compressor_on;
    uint16_t psi[SIDE_AIR_PRESSURE_COUNT];
} side_air_state_t;

/* Where the Air Ride page gets its readings and sends its commands. The right
 * display talks to the Air Ride radio itself; the left goes through the P4. */
typedef struct {
    void (*get_state)(side_air_state_t *state);
    /* Called every 150 ms while a button is held, then once with RELEASE. */
    void (*command)(cluster_air_ride_command_code_t code);
} side_air_backend_t;

typedef struct {
    cluster_display_target_t display; /* LEFT or RIGHT: picks brightness */
    cluster_display_view_t first_view;
    const side_air_backend_t *air;
} side_pages_config_t;

/* side_display builder; context is a side_pages_config_t that must outlive
 * the display. */
void side_pages_create(lv_obj_t *screen, void *context);

/* These take the LVGL lock themselves and are safe from any task. */
void side_pages_show_view(cluster_display_view_t view);
void side_pages_set_center_online(bool online);
void side_pages_apply_settings(const cluster_left_settings_t *settings);
void side_pages_update_vehicle(const cluster_vehicle_fast_t *vehicle);

/* Air Ride controls are live only while the P4 and the controller are both
 * online. Lock-free. */
bool side_pages_air_controls_armed(void);
/* Adjustment overlay state, reported to the P4 by the right display. */
void side_pages_get_air_adjustment(bool *open, uint8_t *target);
/* A remote (knob/phone) command opens the overlay on its target while the
 * car is moving and closes it on release. */
void side_pages_remote_air_adjustment(bool active, uint8_t target);

#ifdef __cplusplus
}
#endif
