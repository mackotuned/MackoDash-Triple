#pragma once

#include <stdbool.h>

#include "cluster_protocol.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Over-the-air updates. The P4 stages an uploaded image and offers it to the
 * board it is for; that board downloads it from the P4's image server,
 * installs it into its spare app slot and restarts. See docs/OTA.md. */

#define CLUSTER_OTA_SERVER "192.168.4.1"
#define CLUSTER_OTA_IMAGE_PORT 8070
#define CLUSTER_OTA_IMAGE_URL "http://" CLUSTER_OTA_SERVER ":8070/image"
#define CLUSTER_OTA_REPORT_URL "http://" CLUSTER_OTA_SERVER "/api/ota/report"

/* "center", "left", "right", "knob" or "climate". */
const char *cluster_ota_target_name(cluster_ota_target_t target);

/* Downloads and installs the staged image in a background task, reporting
 * progress to the P4, then restarts. The image must be built from the same
 * project as the running firmware. Returns ESP_ERR_INVALID_STATE while an
 * update is already running. */
esp_err_t cluster_ota_start(cluster_ota_target_t self);

/* Posts a progress report to the P4; failures are ignored. */
void cluster_ota_report(cluster_ota_target_t self, const char *state,
                        int percent, const char *message);

/* Call once the new firmware has proved itself (display up, network
 * joined). Until then a reset rolls back to the previous firmware. */
void cluster_ota_mark_valid(void);

#ifdef __cplusplus
}
#endif
