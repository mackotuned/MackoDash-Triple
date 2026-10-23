#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "hondata_can.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HONDATA_LOGGER_SAMPLE_PERIOD_MS 100U

typedef struct {
    bool mounted;
    bool recording;
    uint16_t session_index;
    uint32_t record_count;
    uint32_t dropped_count;
} hondata_logger_status_t;

esp_err_t hondata_logger_init(const char *sd_mount_path);
esp_err_t hondata_logger_set_recording(bool recording, uint8_t log_name,
                                       uint32_t uptime_ms);
void hondata_logger_sample(const hondata_state_t *state, uint32_t uptime_ms);
void hondata_logger_get_status(hondata_logger_status_t *status);
bool hondata_logger_get_latest(hondata_state_t *state, uint32_t *uptime_ms);

#ifdef __cplusplus
}
#endif