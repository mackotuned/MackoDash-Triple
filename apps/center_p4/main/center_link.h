#pragma once

#include "cluster_protocol.h"

/* Starts the task that handles knob commands and Air Ride state reports. */
void center_link_start(void);

/* Periodic broadcasts from the main loop. They are dropped quietly while the
 * wireless link is down. */
void center_link_send_vehicle(const cluster_vehicle_fast_t *vehicle);
/* Heartbeat, logging status and display settings, once per second. */
void center_link_send_status(void);
