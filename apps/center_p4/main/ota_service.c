#include "ota_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "cluster_ota.h"
#include "cluster_protocol.h"
#include "cluster_wireless.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define MIN_IMAGE_SIZE (64U * 1024U)
#define MAX_IMAGE_SIZE (12U * 1024U * 1024U)
#define RECEIVE_CHUNK 8192
/* A board that was offered an image must report within this time. */
#define OFFER_TIMEOUT_US (30LL * 1000000LL)
#define RESTART_DELAY_US (1500LL * 1000LL)

typedef struct {
    const char *project;
    esp_chip_id_t chip;
    cluster_wireless_role_t role; /* who receives the offer */
} target_info_t;

static const target_info_t targets[CLUSTER_OTA_TARGET_COUNT] = {
    [CLUSTER_OTA_CENTER] = {"mackodash_triple_center", ESP_CHIP_ID_ESP32P4, 0},
    [CLUSTER_OTA_LEFT] = {"mackodash_triple_left", ESP_CHIP_ID_ESP32S3,
                          CLUSTER_WIRELESS_ROLE_LEFT},
    [CLUSTER_OTA_RIGHT] = {"mackodash_triple_right", ESP_CHIP_ID_ESP32S3,
                           CLUSTER_WIRELESS_ROLE_RIGHT},
    [CLUSTER_OTA_KNOB] = {"mackodash_knob", ESP_CHIP_ID_ESP32S3,
                          CLUSTER_WIRELESS_ROLE_KNOB},
    /* The climate node is reached through its gateway, the left display. */
    [CLUSTER_OTA_CLIMATE] = {"mackodash_hvac_node", ESP_CHIP_ID_ESP32,
                             CLUSTER_WIRELESS_ROLE_LEFT},
};

static const char *TAG = "ota_service";
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
typedef struct {
    char target[12];
    char state[16];
    int percent;
    char message[64];
    char version[32];
    int64_t changed_us;
} ota_status_t;
static ota_status_t status = {.state = "idle"};

/* Replaced only by an upload, which is refused while a board may still be
 * downloading the current image. */
static uint8_t *staged_image;
static size_t staged_size;
static httpd_handle_t image_server;
static esp_timer_handle_t restart_timer;

static void set_status(const char *target, const char *state, int percent,
                       const char *message, const char *version)
{
    taskENTER_CRITICAL(&status_lock);
    if (target) strlcpy(status.target, target, sizeof(status.target));
    strlcpy(status.state, state, sizeof(status.state));
    status.percent = percent;
    strlcpy(status.message, message ? message : "", sizeof(status.message));
    if (version) strlcpy(status.version, version, sizeof(status.version));
    status.changed_us = esp_timer_get_time();
    taskEXIT_CRITICAL(&status_lock);
}

/* A board is mid-transfer until it reports done or error, or goes quiet. */
static bool transfer_active(void)
{
    taskENTER_CRITICAL(&status_lock);
    const bool active = (strcmp(status.state, "offered") == 0 ||
                         strcmp(status.state, "downloading") == 0 ||
                         strcmp(status.state, "installing") == 0) &&
                        esp_timer_get_time() - status.changed_us <
                            OFFER_TIMEOUT_US;
    taskEXIT_CRITICAL(&status_lock);
    return active;
}

static bool parse_target(const char *name, cluster_ota_target_t *target)
{
    for (size_t index = 0; index < CLUSTER_OTA_TARGET_COUNT; ++index) {
        if (strcmp(name, cluster_ota_target_name(index)) == 0) {
            *target = (cluster_ota_target_t)index;
            return true;
        }
    }
    return false;
}

/* Checks the image header and app description against the chosen board. */
static const char *validate_image(const uint8_t *image, size_t size,
                                  cluster_ota_target_t target)
{
    const size_t desc_offset = sizeof(esp_image_header_t) +
                               sizeof(esp_image_segment_header_t);
    if (size < desc_offset + sizeof(esp_app_desc_t)) return "image too small";
    const esp_image_header_t *header = (const esp_image_header_t *)image;
    if (header->magic != ESP_IMAGE_HEADER_MAGIC) return "not a firmware image";
    if (header->chip_id != targets[target].chip) return "image is for another chip";
    const esp_app_desc_t *desc = (const esp_app_desc_t *)(image + desc_offset);
    if (desc->magic_word != ESP_APP_DESC_MAGIC_WORD) return "image has no app description";
    if (strncmp(desc->project_name, targets[target].project,
                sizeof(desc->project_name)) != 0) {
        return "image is for another board";
    }
    return NULL;
}

static void restart(void *argument)
{
    (void)argument;
    esp_restart();
}

/* The P4 installs its own image straight from the staging buffer. */
static esp_err_t install_center(const uint8_t *image, size_t size)
{
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (!partition) return ESP_ERR_NOT_FOUND;
    esp_ota_handle_t handle;
    ESP_RETURN_ON_ERROR(esp_ota_begin(partition, size, &handle), TAG, "begin");
    esp_err_t error = esp_ota_write(handle, image, size);
    if (error != ESP_OK) {
        esp_ota_abort(handle);
        return error;
    }
    ESP_RETURN_ON_ERROR(esp_ota_end(handle), TAG, "verify");
    ESP_RETURN_ON_ERROR(esp_ota_set_boot_partition(partition), TAG, "select");
    const esp_timer_create_args_t timer = {.callback = restart, .name = "ota_restart"};
    if (!restart_timer) {
        ESP_RETURN_ON_ERROR(esp_timer_create(&timer, &restart_timer), TAG, "timer");
    }
    return esp_timer_start_once(restart_timer, RESTART_DELAY_US);
}

static esp_err_t send_json(httpd_req_t *request, const char *status_line,
                           cJSON *json)
{
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!body) return httpd_resp_send_500(request);
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Connection", "close");
    if (status_line) httpd_resp_set_status(request, status_line);
    httpd_resp_set_type(request, "application/json");
    const esp_err_t error = httpd_resp_sendstr(request, body);
    free(body);
    return error;
}

static esp_err_t reply(httpd_req_t *request, const char *status_line,
                       bool ok, const char *message)
{
    cJSON *json = cJSON_CreateObject();
    if (!json) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(json, "ok", ok);
    cJSON_AddStringToObject(json, "message", message);
    return send_json(request, status_line, json);
}

static esp_err_t upload_handler(httpd_req_t *request)
{
    char query[48];
    char name[12];
    cluster_ota_target_t target;
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "target", name, sizeof(name)) != ESP_OK ||
        !parse_target(name, &target)) {
        return reply(request, "400 Bad Request", false, "unknown target");
    }
    if (transfer_active()) {
        return reply(request, "409 Conflict", false,
                     "another update is in progress");
    }
    const size_t size = request->content_len;
    if (size < MIN_IMAGE_SIZE || size > MAX_IMAGE_SIZE) {
        return reply(request, "400 Bad Request", false, "bad image size");
    }

    free(staged_image);
    staged_image = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    staged_size = 0;
    if (!staged_image) {
        return reply(request, "500 Internal Server Error", false, "out of memory");
    }
    set_status(name, "uploading", 0, "", NULL);
    size_t received = 0;
    while (received < size) {
        const size_t want = size - received < RECEIVE_CHUNK ? size - received
                                                             : RECEIVE_CHUNK;
        const int count = httpd_req_recv(request, (char *)staged_image + received,
                                         want);
        if (count == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (count <= 0) {
            set_status(name, "error", 0, "upload interrupted", NULL);
            return ESP_FAIL;
        }
        received += (size_t)count;
    }

    const char *problem = validate_image(staged_image, size, target);
    if (problem) {
        set_status(name, "error", 0, problem, NULL);
        return reply(request, "400 Bad Request", false, problem);
    }
    staged_size = size;
    ESP_LOGI(TAG, "Staged %u byte image for %s", (unsigned)size, name);

    if (target == CLUSTER_OTA_CENTER) {
        set_status(name, "installing", 100, "", NULL);
        const esp_err_t error = install_center(staged_image, size);
        if (error != ESP_OK) {
            set_status(name, "error", 0, esp_err_to_name(error), NULL);
            return reply(request, "500 Internal Server Error", false,
                         "install failed");
        }
        set_status(name, "done", 100, "restarting", NULL);
        return reply(request, NULL, true, "installed; restarting");
    }

    const cluster_ota_offer_t offer = {.target = target, .image_size = size};
    uint8_t payload[CLUSTER_OTA_OFFER_PAYLOAD_SIZE];
    if (cluster_ota_offer_encode(&offer, payload, sizeof(payload)) !=
            CLUSTER_PROTOCOL_OK ||
        cluster_wireless_send_message(targets[target].role,
                                      CLUSTER_MSG_OTA_OFFER, payload,
                                      sizeof(payload)) != ESP_OK) {
        set_status(name, "error", 0, "board is not connected", NULL);
        return reply(request, "503 Service Unavailable", false,
                     "board is not connected");
    }
    set_status(name, "offered", 0, "", NULL);
    return reply(request, "202 Accepted", true, "offered");
}

static esp_err_t report_handler(httpd_req_t *request)
{
    char body[256];
    if (request->content_len <= 0 || request->content_len >= (int)sizeof(body)) {
        return reply(request, "400 Bad Request", false, "bad report");
    }
    int received = 0;
    while (received < request->content_len) {
        const int count = httpd_req_recv(request, body + received,
                                         request->content_len - received);
        if (count <= 0) return ESP_FAIL;
        received += count;
    }
    body[received] = '\0';
    cJSON *json = cJSON_Parse(body);
    const cJSON *target = cJSON_GetObjectItemCaseSensitive(json, "target");
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(json, "state");
    const cJSON *percent = cJSON_GetObjectItemCaseSensitive(json, "percent");
    const cJSON *message = cJSON_GetObjectItemCaseSensitive(json, "message");
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
    if (cJSON_IsString(target) && cJSON_IsString(state)) {
        set_status(target->valuestring, state->valuestring,
                   cJSON_IsNumber(percent) ? percent->valueint : 0,
                   cJSON_IsString(message) ? message->valuestring : "",
                   cJSON_IsString(version) ? version->valuestring : NULL);
    }
    cJSON_Delete(json);
    return reply(request, NULL, true, "ok");
}

static esp_err_t status_handler(httpd_req_t *request)
{
    taskENTER_CRITICAL(&status_lock);
    if (strcmp(status.state, "offered") == 0 &&
        esp_timer_get_time() - status.changed_us > OFFER_TIMEOUT_US) {
        strlcpy(status.state, "error", sizeof(status.state));
        strlcpy(status.message, "board did not respond", sizeof(status.message));
    }
    const ota_status_t copy = status;
    taskEXIT_CRITICAL(&status_lock);

    cJSON *json = cJSON_CreateObject();
    if (!json) return httpd_resp_send_500(request);
    cJSON_AddStringToObject(json, "target", copy.target);
    cJSON_AddStringToObject(json, "state", copy.state);
    cJSON_AddNumberToObject(json, "percent", copy.percent);
    cJSON_AddStringToObject(json, "message", copy.message);
    cJSON_AddStringToObject(json, "version", copy.version);
    cJSON_AddStringToObject(json, "centerVersion",
                            esp_app_get_description()->version);
    return send_json(request, NULL, json);
}

static esp_err_t image_handler(httpd_req_t *request)
{
    if (!staged_image || staged_size == 0) {
        httpd_resp_send_404(request);
        return ESP_OK;
    }
    httpd_resp_set_type(request, "application/octet-stream");
    return httpd_resp_send(request, (const char *)staged_image, staged_size);
}

esp_err_t ota_service_register(httpd_handle_t server)
{
    const httpd_uri_t handlers[OTA_SERVICE_URI_COUNT] = {
        {.uri = "/api/ota/upload", .method = HTTP_POST, .handler = upload_handler},
        {.uri = "/api/ota/report", .method = HTTP_POST, .handler = report_handler},
        {.uri = "/api/ota/status", .method = HTTP_GET, .handler = status_handler},
    };
    for (size_t index = 0; index < OTA_SERVICE_URI_COUNT; ++index) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &handlers[index]),
                            TAG, "register OTA URI");
    }

    /* Boards download from a separate server so a long transfer never
     * blocks the phone UI. */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CLUSTER_OTA_IMAGE_PORT;
    config.ctrl_port = config.ctrl_port + 1;
    config.max_open_sockets = 2;
    config.max_uri_handlers = 1;
    config.send_wait_timeout = 10;
    ESP_RETURN_ON_ERROR(httpd_start(&image_server, &config), TAG,
                        "start image server");
    const httpd_uri_t image = {.uri = "/image", .method = HTTP_GET,
                               .handler = image_handler};
    return httpd_register_uri_handler(image_server, &image);
}
