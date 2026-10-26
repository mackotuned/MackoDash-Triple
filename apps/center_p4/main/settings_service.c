#include "settings_service.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

#include "cJSON.h"
#include "cluster_secrets.h"
#include "dns_server.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_rom_crc.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ota_service.h"

#define SETTINGS_RECORD_MAGIC 0x4D445354UL
#define SETTINGS_NAMESPACE "cluster"
#define SETTINGS_NVS_KEY "settings"
#define SETTINGS_AP_SSID CLUSTER_WIFI_SSID
#define SETTINGS_AP_PASSWORD CLUSTER_WIFI_PASSWORD
#define SETTINGS_PORTAL_URI "http://192.168.4.1/"

/* 0 (default): phones see MackoDash as an ordinary Wi-Fi network without
 * Internet. iPhone/Android stay joined with no sign-in pop-up, keep using
 * cellular data, and the home-screen web app opens the UI full-screen.
 * DNS answers only "mackodash"/"mackodash.local" (NXDOMAIN otherwise) and
 * DHCP no longer advertises a captive-portal URI.
 * 1: previous captive-portal behaviour (wildcard DNS + DHCP option 114),
 * which makes phones open the UI in a pop-up when they join. */
#ifndef SETTINGS_CAPTIVE_POPUP
#define SETTINGS_CAPTIVE_POPUP 0
#endif

typedef struct {
    uint32_t magic;
    uint16_t schema_version;
    uint16_t settings_size;
    cluster_settings_t settings;
    uint32_t crc32;
} settings_record_t;

extern const uint8_t phone_ui_html_start[] asm("_binary_phone_ui_html_start");
extern const uint8_t phone_ui_html_end[] asm("_binary_phone_ui_html_end");
extern const uint8_t apple_touch_icon_png_start[] asm("_binary_apple_touch_icon_png_start");
extern const uint8_t apple_touch_icon_png_end[] asm("_binary_apple_touch_icon_png_end");

/* Web app manifest: lets "Add to Home Screen" install MackoDash as a
 * full-screen app (iOS 16.4+ and Android Chrome read it). */
static const char WEB_MANIFEST[] =
    "{\"name\":\"MackoDash\",\"short_name\":\"MackoDash\",\"start_url\":\"/\","
    "\"scope\":\"/\",\"display\":\"standalone\",\"orientation\":\"any\","
    "\"background_color\":\"#070708\",\"theme_color\":\"#070708\","
    "\"icons\":[{\"src\":\"/apple-touch-icon.png\",\"sizes\":\"180x180\","
    "\"type\":\"image/png\",\"purpose\":\"any\"}]}";

static const char *TAG = "settings_service";
static cluster_settings_t current_settings;
static atomic_bool simulation_enabled = ATOMIC_VAR_INIT(false);
static SemaphoreHandle_t settings_mutex;
static settings_changed_cb_t changed_callback;
static void *changed_context;
static settings_air_state_cb_t air_state_callback;
static settings_air_command_cb_t air_command_callback;
static void *air_callback_context;
static settings_climate_state_cb_t climate_state_callback;
static settings_climate_command_cb_t climate_command_callback;
static esp_netif_t *ap_netif;
static httpd_handle_t http_server;
static dns_server_handle_t dns_server;

static uint16_t next_revision(uint16_t revision)
{
    return revision == UINT16_MAX ? 1U : revision + 1U;
}

static uint32_t record_crc(const settings_record_t *record)
{
    return esp_rom_crc32_le(0U, (const uint8_t *)record,
                            offsetof(settings_record_t, crc32));
}

static esp_err_t settings_save_locked(void)
{
    settings_record_t record = {
        .magic = SETTINGS_RECORD_MAGIC,
        .schema_version = CLUSTER_SETTINGS_SCHEMA_VERSION,
        .settings_size = sizeof(cluster_settings_t),
        .settings = current_settings,
    };
    record.crc32 = record_crc(&record);

    nvs_handle_t handle;
    esp_err_t error = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    error = nvs_set_blob(handle, SETTINGS_NVS_KEY, &record, sizeof(record));
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

/* A stored record is the header, the settings struct as it was when saved,
 * then a CRC over both. Records from the previous schema are a prefix of the
 * current struct, so they load with the new fields at their defaults. */
static bool record_load(const uint8_t *blob, size_t size,
                        cluster_settings_t *settings, uint16_t *version)
{
    const size_t header_size = offsetof(settings_record_t, settings);
    settings_record_t header;
    if (size < header_size + sizeof(uint32_t)) return false;
    memcpy(&header, blob, header_size);
    const size_t settings_size = header.settings_size;
    const bool current = header.schema_version == CLUSTER_SETTINGS_SCHEMA_VERSION &&
                         settings_size == sizeof(cluster_settings_t);
    const bool previous =
        header.schema_version == CLUSTER_SETTINGS_SCHEMA_PREVIOUS_VERSION &&
        settings_size < sizeof(cluster_settings_t);
    if (header.magic != SETTINGS_RECORD_MAGIC || (!current && !previous) ||
        size != header_size + settings_size + sizeof(uint32_t)) {
        return false;
    }
    uint32_t crc;
    memcpy(&crc, blob + header_size + settings_size, sizeof(crc));
    if (crc != esp_rom_crc32_le(0U, blob, header_size + settings_size)) {
        return false;
    }
    cluster_settings_set_defaults(settings);
    memcpy(settings, blob + header_size, settings_size);
    *version = header.schema_version;
    return true;
}

static esp_err_t settings_load(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES ||
        error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS");
        error = nvs_flash_init();
    }
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;

    static uint8_t blob[sizeof(settings_record_t)];
    size_t size = sizeof(blob);
    nvs_handle_t handle;
    error = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_OK) {
        error = nvs_get_blob(handle, SETTINGS_NVS_KEY, blob, &size);
        nvs_close(handle);
    }

    uint16_t version = 0U;
    if (error == ESP_OK && record_load(blob, size, &current_settings, &version)) {
        if (version != CLUSTER_SETTINGS_SCHEMA_VERSION) {
            current_settings.schema_version = CLUSTER_SETTINGS_SCHEMA_VERSION;
            cluster_settings_validate(&current_settings);
            ESP_LOGI(TAG, "Migrated settings schema %u to %u", version,
                     CLUSTER_SETTINGS_SCHEMA_VERSION);
            return settings_save_locked();
        }
        if (!cluster_settings_validate(&current_settings)) {
            ESP_LOGW(TAG, "Repaired out-of-range persisted settings");
            return settings_save_locked();
        }
        ESP_LOGI(TAG, "Loaded settings revision %u", current_settings.revision);
        return ESP_OK;
    }

    cluster_settings_set_defaults(&current_settings);
    ESP_LOGI(TAG, "Using settings defaults (%s)", esp_err_to_name(error));
    return settings_save_locked();
}

void settings_service_get(cluster_settings_t *settings)
{
    if (!settings || !settings_mutex) return;
    xSemaphoreTake(settings_mutex, portMAX_DELAY);
    *settings = current_settings;
    xSemaphoreGive(settings_mutex);
}

esp_err_t settings_service_set_all_brightness(uint8_t brightness,
                                              bool persist)
{
    if (!settings_mutex || brightness < 5U || brightness > 100U) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(settings_mutex, portMAX_DELAY);
    cluster_settings_t updated = current_settings;
    for (size_t index = 0; index < CLUSTER_SCREEN_COUNT; ++index) {
        updated.appearance.brightness[index] = brightness;
    }
    updated.revision = next_revision(current_settings.revision);
    current_settings = updated;
    esp_err_t error = persist ? settings_save_locked() : ESP_OK;
    xSemaphoreGive(settings_mutex);

    if (error == ESP_OK && changed_callback) {
        changed_callback(&updated, changed_context);
    }
    return error;
}

bool settings_service_simulation_enabled(void)
{
    return atomic_load_explicit(&simulation_enabled, memory_order_acquire);
}

/* Adds a JSON array of numbers from any integer array. */
#define JSON_ADD_ARRAY(parent, name, values, count)                             do {                                                                            cJSON *array_ = cJSON_AddArrayToObject((parent), (name));                   for (size_t index_ = 0; index_ < (count); ++index_) {                           cJSON_AddItemToArray(array_, cJSON_CreateNumber((values)[index_]));         }                                                                       } while (0)

static cJSON *settings_to_json(const cluster_settings_t *settings)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schemaVersion", settings->schema_version);
    cJSON_AddNumberToObject(root, "revision", settings->revision);
    cJSON_AddNumberToObject(root, "canProtocol", settings->can_protocol);

    cJSON *units = cJSON_AddObjectToObject(root, "units");
    cJSON_AddBoolToObject(units, "speedKph", settings->units.speed_kph);
    cJSON_AddBoolToObject(units, "temperatureCelsius", settings->units.temperature_celsius);
    cJSON_AddBoolToObject(units, "pressureKpa", settings->units.pressure_kpa);
    cJSON_AddBoolToObject(units, "distanceKm", settings->units.distance_km);

    cJSON *appearance = cJSON_AddObjectToObject(root, "appearance");
    cJSON_AddNumberToObject(appearance, "theme", settings->appearance.theme);
    cJSON_AddNumberToObject(appearance, "tachRgb", settings->appearance.tach_rgb);
    cJSON_AddNumberToObject(appearance, "cautionRgb", settings->appearance.caution_rgb);
    cJSON_AddNumberToObject(appearance, "criticalRgb", settings->appearance.critical_rgb);
    cJSON_AddNumberToObject(appearance, "offlineRgb", settings->appearance.offline_rgb);
    JSON_ADD_ARRAY(appearance, "brightness", settings->appearance.brightness,
                   CLUSTER_SCREEN_COUNT);

    cJSON *engine = cJSON_AddObjectToObject(root, "engine");
    cJSON_AddNumberToObject(engine, "vtecRpm", settings->engine.vtec_rpm);
    cJSON_AddNumberToObject(engine, "redlineRpm", settings->engine.redline_rpm);
    cJSON_AddBoolToObject(engine, "shiftLightEnabled", settings->engine.shift_light_enabled);
    JSON_ADD_ARRAY(engine, "shiftStageRpm", settings->engine.shift_stage_rpm,
                   CLUSTER_SHIFT_STAGE_COUNT);
    JSON_ADD_ARRAY(engine, "shiftStageRgb", settings->engine.shift_stage_rgb,
                   CLUSTER_SHIFT_STAGE_COUNT);
    cJSON_AddNumberToObject(engine, "shiftLightBrightness", settings->engine.shift_light_brightness);
    cJSON_AddBoolToObject(engine, "redlineFlash", settings->engine.redline_flash);
    cJSON_AddNumberToObject(engine, "smoothing", settings->engine.smoothing);

    cJSON *fuel = cJSON_AddObjectToObject(root, "fuel");
    cJSON_AddNumberToObject(fuel, "analogInput", settings->fuel.analog_input);
    cJSON_AddNumberToObject(fuel, "emptyMv", settings->fuel.empty_mv);
    cJSON_AddNumberToObject(fuel, "fullMv", settings->fuel.full_mv);
    JSON_ADD_ARRAY(root, "warningThresholds", settings->warning_thresholds,
                   CLUSTER_WARNING_THRESHOLD_COUNT);

    cJSON *air = cJSON_AddObjectToObject(root, "airRide");
    cJSON_AddBoolToObject(air, "enabled", settings->air_ride.enabled);
    cJSON_AddNumberToObject(air, "compressorOnPsi", settings->air_ride.compressor_on_psi);
    cJSON_AddNumberToObject(air, "compressorOffPsi", settings->air_ride.compressor_off_psi);
    JSON_ADD_ARRAY(air, "presetPsi", settings->air_ride.preset_psi,
                   CLUSTER_AIR_RIDE_PRESET_COUNT);

    cJSON *logging = cJSON_AddObjectToObject(root, "logging");
    cJSON_AddBoolToObject(logging, "autoRecord", settings->logging.auto_record);
    cJSON_AddNumberToObject(logging, "logName", settings->logging.log_name);

    cJSON *screen_layout = cJSON_AddObjectToObject(root, "screenLayout");
    JSON_ADD_ARRAY(screen_layout, "leftValueChannels",
                   settings->screen_layout.left_value_channels,
                   CLUSTER_LEFT_VALUE_SLOT_COUNT);
    JSON_ADD_ARRAY(screen_layout, "rightValueChannels",
                   settings->screen_layout.right_value_channels,
                   CLUSTER_LEFT_VALUE_SLOT_COUNT);
    JSON_ADD_ARRAY(screen_layout, "leftGaugeChannels",
                   settings->screen_layout.left_gauge_channels,
                   CLUSTER_GAUGE_SLOT_COUNT);
    JSON_ADD_ARRAY(screen_layout, "rightGaugeChannels",
                   settings->screen_layout.right_gauge_channels,
                   CLUSTER_GAUGE_SLOT_COUNT);
    cJSON_AddBoolToObject(screen_layout, "rightBatteryEnabled",
                          settings->screen_layout.right_battery_enabled);
    cJSON *simulation = cJSON_AddObjectToObject(root, "simulation");
    cJSON_AddBoolToObject(simulation, "enabled",
                          settings_service_simulation_enabled());
    return root;
}

static bool json_bool(const cJSON *value, bool *output)
{
    if (cJSON_IsBool(value)) {
        *output = cJSON_IsTrue(value);
        return true;
    }
    if (cJSON_IsNumber(value)) {
        *output = value->valueint != 0;
        return true;
    }
    return false;
}

static bool json_u32(const cJSON *value, uint32_t *output)
{
    if (!cJSON_IsNumber(value) || value->valuedouble < 0.0 ||
        value->valuedouble > 4294967295.0) return false;
    *output = (uint32_t)value->valuedouble;
    return true;
}

static bool indexed_key(const char *key, const char *prefix, size_t count,
                        size_t *index)
{
    const size_t length = strlen(prefix);
    if (strncmp(key, prefix, length) != 0) return false;
    char *end = NULL;
    unsigned long parsed = strtoul(key + length, &end, 10);
    if (!end || *end != '\0' || parsed >= count) return false;
    *index = parsed;
    return true;
}

#define SET_BOOL(path, name) \
    if (strcmp(key, name) == 0) return json_bool(value, &(path))
#define SET_U8(path, name) \
    if (strcmp(key, name) == 0) { uint32_t number; if (!json_u32(value, &number) || number > UINT8_MAX) return false; (path) = (uint8_t)number; return true; }
#define SET_U16(path, name) \
    if (strcmp(key, name) == 0) { uint32_t number; if (!json_u32(value, &number) || number > UINT16_MAX) return false; (path) = (uint16_t)number; return true; }
#define SET_U32(path, name) \
    if (strcmp(key, name) == 0) return json_u32(value, &(path))

static bool apply_patch(cluster_settings_t *settings, const char *key,
                        const cJSON *value)
{
    SET_U8(settings->can_protocol, "canProtocol");
    SET_BOOL(settings->units.speed_kph, "units.speedKph");
    SET_BOOL(settings->units.temperature_celsius, "units.temperatureCelsius");
    SET_BOOL(settings->units.pressure_kpa, "units.pressureKpa");
    SET_BOOL(settings->units.distance_km, "units.distanceKm");
    SET_U8(settings->appearance.theme, "appearance.theme");
    SET_U32(settings->appearance.tach_rgb, "appearance.tachRgb");
    SET_U32(settings->appearance.caution_rgb, "appearance.cautionRgb");
    SET_U32(settings->appearance.critical_rgb, "appearance.criticalRgb");
    SET_U32(settings->appearance.offline_rgb, "appearance.offlineRgb");
    SET_U16(settings->engine.vtec_rpm, "engine.vtecRpm");
    SET_U16(settings->engine.redline_rpm, "engine.redlineRpm");
    SET_BOOL(settings->engine.shift_light_enabled, "engine.shiftLightEnabled");
    SET_U8(settings->engine.shift_light_brightness, "engine.shiftLightBrightness");
    SET_BOOL(settings->engine.redline_flash, "engine.redlineFlash");
    SET_U8(settings->engine.smoothing, "engine.smoothing");
    SET_U8(settings->fuel.analog_input, "fuel.analogInput");
    SET_U16(settings->fuel.empty_mv, "fuel.emptyMv");
    SET_U16(settings->fuel.full_mv, "fuel.fullMv");
    SET_BOOL(settings->air_ride.enabled, "airRide.enabled");
    SET_U16(settings->air_ride.compressor_on_psi, "airRide.compressorOnPsi");
    SET_U16(settings->air_ride.compressor_off_psi, "airRide.compressorOffPsi");
    SET_BOOL(settings->logging.auto_record, "logging.autoRecord");
    SET_U8(settings->logging.log_name, "logging.logName");

    size_t index;
    uint32_t number;
    if (indexed_key(key, "appearance.brightness.", CLUSTER_SCREEN_COUNT, &index) &&
        json_u32(value, &number) && number <= UINT8_MAX) {
        settings->appearance.brightness[index] = (uint8_t)number;
        return true;
    }
    if (indexed_key(key, "engine.shiftStageRpm.", CLUSTER_SHIFT_STAGE_COUNT, &index) &&
        json_u32(value, &number) && number <= UINT16_MAX) {
        settings->engine.shift_stage_rpm[index] = (uint16_t)number;
        return true;
    }
    if (indexed_key(key, "engine.shiftStageRgb.", CLUSTER_SHIFT_STAGE_COUNT, &index) &&
        json_u32(value, &number)) {
        settings->engine.shift_stage_rgb[index] = number;
        return true;
    }
    if (indexed_key(key, "warningThresholds.", CLUSTER_WARNING_THRESHOLD_COUNT, &index) &&
        cJSON_IsNumber(value) && value->valuedouble >= INT16_MIN && value->valuedouble <= INT16_MAX) {
        settings->warning_thresholds[index] = (int16_t)value->valueint;
        return true;
    }
    if (indexed_key(key, "airRide.presetPsi.", CLUSTER_AIR_RIDE_PRESET_COUNT, &index) &&
        json_u32(value, &number) && number <= UINT16_MAX) {
        settings->air_ride.preset_psi[index] = (uint16_t)number;
        return true;
    }
    static const struct {
        const char *prefix;
        size_t offset;
        size_t count;
    } channel_lists[] = {
        {"screenLayout.leftValueChannels.",
         offsetof(cluster_screen_layout_settings_t, left_value_channels),
         CLUSTER_LEFT_VALUE_SLOT_COUNT},
        {"screenLayout.rightValueChannels.",
         offsetof(cluster_screen_layout_settings_t, right_value_channels),
         CLUSTER_LEFT_VALUE_SLOT_COUNT},
        {"screenLayout.leftGaugeChannels.",
         offsetof(cluster_screen_layout_settings_t, left_gauge_channels),
         CLUSTER_GAUGE_SLOT_COUNT},
        {"screenLayout.rightGaugeChannels.",
         offsetof(cluster_screen_layout_settings_t, right_gauge_channels),
         CLUSTER_GAUGE_SLOT_COUNT},
    };
    for (size_t list = 0; list < sizeof(channel_lists) / sizeof(channel_lists[0]);
         ++list) {
        if (indexed_key(key, channel_lists[list].prefix,
                        channel_lists[list].count, &index) &&
            json_u32(value, &number) && number < VEHICLE_DATA_COUNT) {
            vehicle_data_channel_t *channels = (vehicle_data_channel_t *)(
                (uint8_t *)&settings->screen_layout + channel_lists[list].offset);
            channels[index] = (vehicle_data_channel_t)number;
            return true;
        }
    }
    SET_BOOL(settings->screen_layout.right_battery_enabled,
             "screenLayout.rightBatteryEnabled");
    return false;
}

/* Every response closes its connection. Browsers otherwise park idle
 * keep-alive sockets on the server; once the small socket pool is full the
 * LRU purge closes one the page is about to reuse, and that request fails
 * with ERR_EMPTY_RESPONSE. Call first in each handler so error replies are
 * covered too. */
static void set_common_headers(httpd_req_t *request)
{
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Connection", "close");
}

static esp_err_t send_settings_json(httpd_req_t *request)
{
    cluster_settings_t settings;
    settings_service_get(&settings);
    cJSON *json = settings_to_json(&settings);
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!body) return httpd_resp_send_500(request);
    httpd_resp_set_type(request, "application/json");
    esp_err_t error = httpd_resp_sendstr(request, body);
    free(body);
    return error;
}

static esp_err_t settings_get_handler(httpd_req_t *request)
{
    set_common_headers(request);
    return send_settings_json(request);
}

static esp_err_t settings_post_handler(httpd_req_t *request)
{
    set_common_headers(request);
    if (request->content_len <= 0 || request->content_len > 256) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid body size");
    }
    char body[257];
    int received = httpd_req_recv(request, body, request->content_len);
    if (received != request->content_len) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Incomplete body");
    }
    body[received] = '\0';
    cJSON *json = cJSON_Parse(body);
    cJSON *key = json ? cJSON_GetObjectItemCaseSensitive(json, "key") : NULL;
    cJSON *value = json ? cJSON_GetObjectItemCaseSensitive(json, "value") : NULL;
    if (!cJSON_IsString(key) || !value) {
        cJSON_Delete(json);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Expected key and value");
    }

    xSemaphoreTake(settings_mutex, portMAX_DELAY);
    cluster_settings_t updated = current_settings;
    bool simulation_value = false;
    bool simulation_update = strcmp(key->valuestring, "simulation.enabled") == 0;
    bool accepted = simulation_update
        ? json_bool(value, &simulation_value)
        : apply_patch(&updated, key->valuestring, value);
    if (accepted && simulation_update) {
        atomic_store_explicit(&simulation_enabled, simulation_value,
                              memory_order_release);
        ESP_LOGI(TAG, "Real-time simulation %s",
                 simulation_value ? "enabled" : "disabled");
    } else if (accepted) {
        updated.revision = next_revision(current_settings.revision);
        cluster_settings_validate(&updated);
        current_settings = updated;
        esp_err_t error = settings_save_locked();
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "Settings save failed: %s", esp_err_to_name(error));
        }
    }
    xSemaphoreGive(settings_mutex);
    cJSON_Delete(json);
    if (!accepted) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Unknown or invalid setting");
    }
    if (!simulation_update && changed_callback) {
        changed_callback(&updated, changed_context);
    }
    return send_settings_json(request);
}

static esp_err_t reset_post_handler(httpd_req_t *request)
{
    set_common_headers(request);
    xSemaphoreTake(settings_mutex, portMAX_DELAY);
    cluster_settings_set_defaults(&current_settings);
    esp_err_t error = settings_save_locked();
    cluster_settings_t updated = current_settings;
    xSemaphoreGive(settings_mutex);
    if (error != ESP_OK) return httpd_resp_send_500(request);
    if (changed_callback) changed_callback(&updated, changed_context);
    return send_settings_json(request);
}

static esp_err_t root_get_handler(httpd_req_t *request)
{
    set_common_headers(request);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, (const char *)phone_ui_html_start,
                           phone_ui_html_end - phone_ui_html_start);
}

static esp_err_t icon_get_handler(httpd_req_t *request)
{
    /* Static asset: let the phone cache it instead of no-store. */
    httpd_resp_set_hdr(request, "Connection", "close");
    httpd_resp_set_hdr(request, "Cache-Control", "max-age=86400");
    httpd_resp_set_type(request, "image/png");
    return httpd_resp_send(request, (const char *)apple_touch_icon_png_start,
                           apple_touch_icon_png_end - apple_touch_icon_png_start);
}

static esp_err_t manifest_get_handler(httpd_req_t *request)
{
    set_common_headers(request);
    httpd_resp_set_type(request, "application/manifest+json");
    return httpd_resp_sendstr(request, WEB_MANIFEST);
}

static esp_err_t not_found_handler(httpd_req_t *request, httpd_err_code_t error)
{
    (void)error;
    set_common_headers(request);
    httpd_resp_set_status(request, "303 See Other");
    httpd_resp_set_hdr(request, "Location", SETTINGS_PORTAL_URI);
    return httpd_resp_sendstr(request, "Open MackoDash Control");
}

static esp_err_t air_command_handler(httpd_req_t *request)
{
    set_common_headers(request);
    if (!air_command_callback || request->content_len <= 0 ||
        request->content_len > 96) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Invalid Air Ride command");
    }
    char body[97];
    const int received = httpd_req_recv(request, body, request->content_len);
    if (received != request->content_len) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Incomplete body");
    }
    body[received] = '\0';
    cJSON *json = cJSON_Parse(body);
    const cJSON *value = json
        ? cJSON_GetObjectItemCaseSensitive(json, "command") : NULL;
    static const struct {
        const char *name;
        cluster_air_ride_command_code_t code;
    } commands[] = {
        {"compressor_off", CLUSTER_AIR_RIDE_COMPRESSOR_OFF},
        {"compressor_on", CLUSTER_AIR_RIDE_COMPRESSOR_ON},
        {"front_left_up", CLUSTER_AIR_RIDE_FRONT_LEFT_UP},
        {"front_left_down", CLUSTER_AIR_RIDE_FRONT_LEFT_DOWN},
        {"front_right_up", CLUSTER_AIR_RIDE_FRONT_RIGHT_UP},
        {"front_right_down", CLUSTER_AIR_RIDE_FRONT_RIGHT_DOWN},
        {"rear_left_up", CLUSTER_AIR_RIDE_REAR_LEFT_UP},
        {"rear_left_down", CLUSTER_AIR_RIDE_REAR_LEFT_DOWN},
        {"rear_right_up", CLUSTER_AIR_RIDE_REAR_RIGHT_UP},
        {"rear_right_down", CLUSTER_AIR_RIDE_REAR_RIGHT_DOWN},
        {"all_up", CLUSTER_AIR_RIDE_ALL_UP},
        {"all_down", CLUSTER_AIR_RIDE_ALL_DOWN},
        /* FIRMWARE REVIEW: axle-pair commands already exist in the wire enum
         * and are used by the knob; exposed here for the phone UI. */
        {"front_up", CLUSTER_AIR_RIDE_FRONT_UP},
        {"front_down", CLUSTER_AIR_RIDE_FRONT_DOWN},
        {"rear_up", CLUSTER_AIR_RIDE_REAR_UP},
        {"rear_down", CLUSTER_AIR_RIDE_REAR_DOWN},
        {"release", CLUSTER_AIR_RIDE_RELEASE},
    };
    bool found = false;
    cluster_air_ride_command_code_t command = CLUSTER_AIR_RIDE_RELEASE;
    if (cJSON_IsString(value)) {
        for (size_t index = 0; index < sizeof(commands) / sizeof(commands[0]);
             ++index) {
            if (strcmp(value->valuestring, commands[index].name) == 0) {
                command = commands[index].code;
                found = true;
                break;
            }
        }
    }
    cJSON_Delete(json);
    if (!found) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Unknown Air Ride command");
    }
    if (air_command_callback(command, air_callback_context) != ESP_OK) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_sendstr(request, "Air Ride link is not armed");
    }
    return httpd_resp_sendstr(request, "ok");
}

static esp_err_t air_state_handler(httpd_req_t *request)
{
    set_common_headers(request);
    settings_air_state_t state = {0};
    if (air_state_callback) {
        air_state_callback(&state, air_callback_context);
    }
    cJSON *json = cJSON_CreateObject();
    if (!json) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(json, "rightOnline", state.right_online);
    cJSON_AddBoolToObject(json, "airConnected", state.air_connected);
    cJSON_AddBoolToObject(json, "pressureValid", state.pressure_valid);
    cJSON_AddBoolToObject(json, "compressorValid", state.compressor_valid);
    cJSON_AddBoolToObject(json, "compressorOn", state.compressor_on);
    cJSON_AddNumberToObject(json, "tankPsi", state.tank_psi);
    cJSON_AddNumberToObject(json, "frontLeftPsi", state.front_left_psi);
    cJSON_AddNumberToObject(json, "frontRightPsi", state.front_right_psi);
    cJSON_AddNumberToObject(json, "rearLeftPsi", state.rear_left_psi);
    cJSON_AddNumberToObject(json, "rearRightPsi", state.rear_right_psi);
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!body) return httpd_resp_send_500(request);
    httpd_resp_set_type(request, "application/json");
    const esp_err_t error = httpd_resp_sendstr(request, body);
    free(body);
    return error;
}

static esp_err_t climate_state_handler(httpd_req_t *request)
{
    set_common_headers(request);
    cluster_climate_state_t state = {0};
    const bool online = climate_state_callback && climate_state_callback(&state);
    cJSON *json = cJSON_CreateObject();
    if (!json) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(json, "online", online);
    cJSON_AddNumberToObject(json, "fan", state.target_fan);
    cJSON_AddNumberToObject(json, "temp", state.target_temp);
    cJSON_AddNumberToObject(json, "mode", state.target_mode);
    cJSON_AddBoolToObject(json, "moving", state.moving_mask != 0U);
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!body) return httpd_resp_send_500(request);
    httpd_resp_set_type(request, "application/json");
    const esp_err_t error = httpd_resp_sendstr(request, body);
    free(body);
    return error;
}

static esp_err_t climate_command_handler(httpd_req_t *request)
{
    set_common_headers(request);
    if (!climate_command_callback || request->content_len <= 0 ||
        request->content_len > 96) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Invalid climate command");
    }
    char body[97];
    const int received = httpd_req_recv(request, body, request->content_len);
    if (received != request->content_len) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Incomplete body");
    }
    body[received] = '\0';
    cJSON *json = cJSON_Parse(body);
    const cJSON *name = json ? cJSON_GetObjectItemCaseSensitive(json, "command") : NULL;
    const cJSON *value = json ? cJSON_GetObjectItemCaseSensitive(json, "value") : NULL;
    static const struct {
        const char *name;
        settings_climate_dial_t dial;
    } dials[] = {
        {"fan", SETTINGS_CLIMATE_FAN},
        {"temp", SETTINGS_CLIMATE_TEMP},
        {"mode", SETTINGS_CLIMATE_MODE},
    };
    esp_err_t error = ESP_ERR_INVALID_ARG;
    uint32_t number;
    if (cJSON_IsString(name) && json_u32(value, &number)) {
        for (size_t index = 0; index < sizeof(dials) / sizeof(dials[0]); ++index) {
            if (strcmp(name->valuestring, dials[index].name) == 0) {
                error = climate_command_callback(dials[index].dial, number);
                break;
            }
        }
    }
    cJSON_Delete(json);
    if (error == ESP_ERR_INVALID_ARG) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Unknown or invalid climate command");
    }
    if (error != ESP_OK) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_sendstr(request, "Climate controller is offline");
    }
    return httpd_resp_sendstr(request, "ok");
}

static void wifi_ap_event_handler(void *arg, esp_event_base_t base,
                                  int32_t id, void *event_data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *event = event_data;
        ESP_LOGI(TAG, "Phone " MACSTR " joined MackoDash, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *event = event_data;
        ESP_LOGI(TAG, "Phone " MACSTR " left MackoDash, AID=%d reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    }
}

#if SETTINGS_CAPTIVE_POPUP
static esp_err_t configure_captive_portal(void)
{
    esp_err_t error = esp_netif_dhcps_stop(ap_netif);
    if (error != ESP_OK &&
        error != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) return error;
    error = esp_netif_dhcps_option(
        ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI,
        (void *)SETTINGS_PORTAL_URI, strlen(SETTINGS_PORTAL_URI));
    if (error != ESP_OK) return error;
    error = esp_netif_dhcps_start(ap_netif);
    if (error != ESP_OK &&
        error != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) return error;
    return ESP_OK;
}
#endif

static esp_err_t start_wifi(void)
{
    esp_err_t error = esp_netif_init();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
    ap_netif = esp_netif_create_default_wifi_ap();
    if (!ap_netif) return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_ap_event_handler, NULL, NULL));

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    error = esp_wifi_init(&init);
    if (error != ESP_OK && error != ESP_ERR_WIFI_INIT_STATE) return error;
    wifi_config_t config = {0};
    strlcpy((char *)config.ap.ssid, SETTINGS_AP_SSID, sizeof(config.ap.ssid));
    strlcpy((char *)config.ap.password, SETTINGS_AP_PASSWORD, sizeof(config.ap.password));
    config.ap.ssid_len = strlen(SETTINGS_AP_SSID);
    config.ap.channel = 6;
    config.ap.max_connection = 8;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.pmf_cfg.required = false;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set AP mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &config), TAG, "set AP config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start AP");
#if SETTINGS_CAPTIVE_POPUP
    return configure_captive_portal();
#else
    return ESP_OK;
#endif
}

static esp_err_t start_http(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 16;  /* 11 here plus the OTA service */
    /* httpd needs max_open_sockets + 3 LWIP sockets, and the DNS server and
     * cluster UDP link hold more. With the default CONFIG_LWIP_MAX_SOCKETS=10,
     * the stock value (7) leaves accept() failing under load. 5 fits; with
     * Connection: close each request frees its socket immediately.
     * FIRMWARE REVIEW: raising CONFIG_LWIP_MAX_SOCKETS to 16 adds headroom. */
    config.max_open_sockets = 5;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    ESP_RETURN_ON_ERROR(httpd_start(&http_server, &config), TAG, "start HTTP");
    const httpd_uri_t handlers[] = {
        {.uri = "/", .method = HTTP_GET, .handler = root_get_handler},
        {.uri = "/api/settings", .method = HTTP_GET, .handler = settings_get_handler},
        {.uri = "/api/settings", .method = HTTP_POST, .handler = settings_post_handler},
        {.uri = "/api/reset", .method = HTTP_POST, .handler = reset_post_handler},
        {.uri = "/api/air/state", .method = HTTP_GET, .handler = air_state_handler},
        {.uri = "/api/air/command", .method = HTTP_POST, .handler = air_command_handler},
        {.uri = "/api/climate/state", .method = HTTP_GET, .handler = climate_state_handler},
        {.uri = "/api/climate/command", .method = HTTP_POST, .handler = climate_command_handler},
        {.uri = "/apple-touch-icon.png", .method = HTTP_GET, .handler = icon_get_handler},
        {.uri = "/apple-touch-icon-precomposed.png", .method = HTTP_GET, .handler = icon_get_handler},
        {.uri = "/manifest.json", .method = HTTP_GET, .handler = manifest_get_handler},
    };
    for (size_t index = 0; index < sizeof(handlers) / sizeof(handlers[0]); ++index) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(http_server, &handlers[index]),
                            TAG, "register URI");
    }
    ESP_RETURN_ON_ERROR(
        httpd_register_err_handler(http_server, HTTPD_404_NOT_FOUND,
                                   not_found_handler),
        TAG, "register portal redirect");
    return ota_service_register(http_server);
}

static void network_task(void *argument)
{
    (void)argument;
    esp_err_t error = start_wifi();
    if (error == ESP_OK) error = start_http();
    if (error == ESP_OK) {
#if SETTINGS_CAPTIVE_POPUP
        dns_server_config_t dns_config =
            DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
#else
        dns_server_config_t dns_config =
            DNS_SERVER_CONFIG_PAIR("mackodash", "mackodash.local", "WIFI_AP_DEF");
#endif
        dns_server = start_dns_server(&dns_config);
        if (!dns_server) error = ESP_ERR_NO_MEM;
    }
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "Phone settings ready: Wi-Fi %s, %s",
                 SETTINGS_AP_SSID, SETTINGS_PORTAL_URI);
    } else {
        ESP_LOGE(TAG, "Phone settings unavailable: %s", esp_err_to_name(error));
    }
    vTaskDelete(NULL);
}

esp_err_t settings_service_start(settings_changed_cb_t callback, void *context,
                                 settings_air_state_cb_t state_callback,
                                 settings_air_command_cb_t command_callback,
                                 void *air_context)
{
    settings_mutex = xSemaphoreCreateMutex();
    if (!settings_mutex) return ESP_ERR_NO_MEM;
    changed_callback = callback;
    changed_context = context;
    air_state_callback = state_callback;
    air_command_callback = command_callback;
    air_callback_context = air_context;
    ESP_RETURN_ON_ERROR(settings_load(), TAG, "load settings");
    if (changed_callback) changed_callback(&current_settings, changed_context);
    if (xTaskCreate(network_task, "settings_net", 12288, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void settings_service_set_climate_handlers(
    settings_climate_state_cb_t state_callback,
    settings_climate_command_cb_t command_callback)
{
    climate_state_callback = state_callback;
    climate_command_callback = command_callback;
}
