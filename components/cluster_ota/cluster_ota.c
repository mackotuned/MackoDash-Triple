#include "cluster_ota.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define REPORT_STEP_PERCENT 10

static const char *TAG = "cluster_ota";
static atomic_bool update_running = ATOMIC_VAR_INIT(false);

const char *cluster_ota_target_name(cluster_ota_target_t target)
{
    static const char *const names[CLUSTER_OTA_TARGET_COUNT] = {
        [CLUSTER_OTA_CENTER] = "center",
        [CLUSTER_OTA_LEFT] = "left",
        [CLUSTER_OTA_RIGHT] = "right",
        [CLUSTER_OTA_KNOB] = "knob",
        [CLUSTER_OTA_CLIMATE] = "climate",
    };
    return target < CLUSTER_OTA_TARGET_COUNT ? names[target] : "unknown";
}

void cluster_ota_report(cluster_ota_target_t self, const char *state,
                        int percent, const char *message)
{
    char body[192];
    const int length = snprintf(
        body, sizeof(body),
        "{\"target\":\"%s\",\"state\":\"%s\",\"percent\":%d,"
        "\"message\":\"%s\",\"version\":\"%s\"}",
        cluster_ota_target_name(self), state, percent, message ? message : "",
        esp_app_get_description()->version);
    if (length <= 0 || length >= (int)sizeof(body)) return;

    const esp_http_client_config_t config = {
        .url = CLUSTER_OTA_REPORT_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 2000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return;
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, length);
    esp_http_client_perform(client);
    esp_http_client_cleanup(client);
}

static esp_err_t install(cluster_ota_target_t self)
{
    const esp_http_client_config_t http = {
        .url = CLUSTER_OTA_IMAGE_URL,
        .timeout_ms = 10000,
        .buffer_size = 4096,
        .keep_alive_enable = true,
    };
    const esp_https_ota_config_t config = {.http_config = &http};
    esp_https_ota_handle_t handle = NULL;
    esp_err_t error = esp_https_ota_begin(&config, &handle);
    if (error != ESP_OK) {
        cluster_ota_report(self, "error", 0, "image download failed");
        return error;
    }

    /* Refuse an image built for a different board. */
    esp_app_desc_t image;
    error = esp_https_ota_get_img_desc(handle, &image);
    if (error == ESP_OK &&
        strncmp(image.project_name, esp_app_get_description()->project_name,
                sizeof(image.project_name)) != 0) {
        error = ESP_ERR_INVALID_VERSION;
    }
    if (error != ESP_OK) {
        esp_https_ota_abort(handle);
        cluster_ota_report(self, "error", 0, "image is for another board");
        return error;
    }

    const int size = esp_https_ota_get_image_size(handle);
    int next_report = REPORT_STEP_PERCENT;
    while ((error = esp_https_ota_perform(handle)) ==
           ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        const int percent = size > 0
            ? (int)((int64_t)esp_https_ota_get_image_len_read(handle) * 100 / size)
            : 0;
        if (percent >= next_report) {
            cluster_ota_report(self, "downloading", percent, "");
            next_report = percent + REPORT_STEP_PERCENT;
        }
    }
    if (error != ESP_OK || !esp_https_ota_is_complete_data_received(handle)) {
        esp_https_ota_abort(handle);
        cluster_ota_report(self, "error", 0, "download interrupted");
        return error != ESP_OK ? error : ESP_ERR_INVALID_SIZE;
    }

    /* Finishing verifies the image's SHA-256 and selects it for boot. */
    cluster_ota_report(self, "installing", 100, "");
    error = esp_https_ota_finish(handle);
    if (error != ESP_OK) {
        cluster_ota_report(self, "error", 100, "image failed verification");
        return error;
    }
    cluster_ota_report(self, "done", 100, "restarting");
    return ESP_OK;
}

static void update_task(void *argument)
{
    const cluster_ota_target_t self = (cluster_ota_target_t)(uintptr_t)argument;
    ESP_LOGI(TAG, "Starting over-the-air update");
    const esp_err_t error = install(self);
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "Update installed; restarting");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    ESP_LOGE(TAG, "Update failed: %s", esp_err_to_name(error));
    atomic_store_explicit(&update_running, false, memory_order_release);
    vTaskDelete(NULL);
}

esp_err_t cluster_ota_start(cluster_ota_target_t self)
{
    if (atomic_exchange_explicit(&update_running, true, memory_order_acq_rel)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreate(update_task, "cluster_ota", 8192, (void *)(uintptr_t)self,
                    4, NULL) != pdPASS) {
        atomic_store_explicit(&update_running, false, memory_order_release);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void cluster_ota_mark_valid(void)
{
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "New firmware confirmed");
        esp_ota_mark_app_valid_cancel_rollback();
    }
}
