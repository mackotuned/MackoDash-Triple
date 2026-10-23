#pragma once

#include "cluster_protocol.h"
#include "settings_schema.h"

/* Brings up the 800x800 panel, the tach dial and its overlays. */
void center_display_start(void);

/* settings_service callback: reapplies theme, colours and brightness on the
 * next display tick. */
void center_display_settings_changed(const cluster_settings_t *settings,
                                     void *context);

/* Selects what the center screen shows: the tach, or the Air Ride or log
 * overlay on top of it. Safe from any task. */
void center_display_show_view(cluster_display_view_t view);
void center_display_close_logs(void);
