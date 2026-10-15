#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t side_display_start(void);
esp_err_t side_display_update_link(uint16_t sequence, uint32_t center_uptime_ms);

#ifdef __cplusplus
}
#endif