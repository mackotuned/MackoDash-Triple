#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/* Over-the-air update service on the P4 (see docs/OTA.md).
 *   POST /api/ota/upload?target=NAME   raw firmware image as the body
 *   GET  /api/ota/status               progress of the current update
 *   POST /api/ota/report               progress posted by the board updating
 * Images for other boards are served on port 8070 at /image. */
esp_err_t ota_service_register(httpd_handle_t server);

/* Number of URI handlers ota_service_register adds. */
#define OTA_SERVICE_URI_COUNT 3
