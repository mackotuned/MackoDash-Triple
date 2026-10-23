#include "cluster_wireless.h"

#include <stdatomic.h>
#include <string.h>

#include "cluster_secrets.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

#define CLUSTER_WIRELESS_MAGIC 0x4D57U
#define CLUSTER_WIRELESS_VERSION 1U
#define CLUSTER_WIRELESS_PORT 45500U
#define CLUSTER_WIRELESS_MAX_FRAME_SIZE 270U
#define CLUSTER_WIRELESS_QUEUE_DEPTH 16U

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t version;
    uint8_t source;
    uint8_t destination;
    uint8_t reserved;
    uint16_t frame_size;
    uint8_t frame[CLUSTER_WIRELESS_MAX_FRAME_SIZE];
} cluster_wireless_packet_t;

typedef struct {
    size_t size;
    uint8_t frame[CLUSTER_WIRELESS_MAX_FRAME_SIZE];
} cluster_wireless_queue_item_t;

static const char *TAG = "cluster_wireless";
static QueueHandle_t receive_queue;
static cluster_wireless_config_t transport_config;
static atomic_bool transport_ready = ATOMIC_VAR_INIT(false);
static int transport_socket = -1;
static atomic_uint_fast16_t message_sequence = ATOMIC_VAR_INIT(0U);

static void station_event(void *argument, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    (void)argument;
    (void)event_data;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store_explicit(&transport_ready, false, memory_order_release);
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP &&
               transport_socket >= 0) {
        atomic_store_explicit(&transport_ready, true, memory_order_release);
    }
}

static esp_err_t initialize_wifi(esp_netif_t **station_netif)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES ||
        error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS");
        error = nvs_flash_init();
    }
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
    error = esp_netif_init();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;

    *station_netif = esp_netif_create_default_wifi_sta();
    if (!*station_netif) return ESP_ERR_NO_MEM;

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    error = esp_wifi_init(&wifi_config);
    if (error != ESP_OK && error != ESP_ERR_WIFI_INIT_STATE) return error;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG,
                        "set station mode");
    if (transport_config.station_mac) {
        ESP_RETURN_ON_ERROR(
            esp_wifi_set_mac(WIFI_IF_STA, transport_config.station_mac), TAG,
            "set station MAC");
    }
    error = esp_wifi_start();
    if (error != ESP_OK && error != ESP_ERR_WIFI_CONN) return error;
    return ESP_OK;
}

static esp_err_t connect_to_center(esp_netif_t **station_netif)
{
    if (!*station_netif) {
        *station_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    }
    if (!*station_netif) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                   station_event, NULL),
        TAG, "register Wi-Fi event handler");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                   station_event, NULL),
        TAG, "register IP event handler");
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, CLUSTER_WIFI_SSID,
            sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, CLUSTER_WIFI_PASSWORD,
            sizeof(config.sta.password));
    config.sta.scan_method = WIFI_FAST_SCAN;
    config.sta.channel = CLUSTER_WIRELESS_CHANNEL;
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    esp_err_t error = ESP_OK;
    esp_netif_ip_info_t ip_info = {0};
    bool connection_started = false;
    do {
        if (!connection_started) {
            wifi_scan_config_t scan_config = {
                .ssid = config.sta.ssid,
                .channel = CLUSTER_WIRELESS_CHANNEL,
                .show_hidden = false,
                .scan_type = WIFI_SCAN_TYPE_ACTIVE,
            };
            wifi_ap_record_t access_point = {0};
            uint16_t access_point_count = 1U;
            error = esp_wifi_scan_start(&scan_config, true);
            if (error == ESP_OK) {
                error = esp_wifi_scan_get_ap_records(&access_point_count,
                                                     &access_point);
            }
            if (error == ESP_OK && access_point_count == 1U &&
                access_point.primary == CLUSTER_WIRELESS_CHANNEL) {
                config.sta.bssid_set = true;
                memcpy(config.sta.bssid, access_point.bssid,
                       sizeof(config.sta.bssid));
                ESP_RETURN_ON_ERROR(
                    esp_wifi_set_config(WIFI_IF_STA, &config), TAG,
                    "set channel-6 station config");
                error = esp_wifi_connect();
                if (error != ESP_OK && error != ESP_ERR_WIFI_CONN) {
                    return error;
                }
                connection_started = true;
            } else {
                ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_channel(
                    CLUSTER_WIRELESS_CHANNEL, WIFI_SECOND_CHAN_NONE));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        error = esp_netif_get_ip_info(*station_netif, &ip_info);
    } while (error != ESP_OK || ip_info.ip.addr == 0U);
    ESP_LOGI(TAG, "Joined %s at " IPSTR, CLUSTER_WIFI_SSID,
             IP2STR(&ip_info.ip));
    /* In modem sleep the AP holds broadcast frames until the next DTIM
     * beacon, so gauge data arrives in bursts. Stay awake instead. */
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_ps(WIFI_PS_NONE));
    return ESP_OK;
}

static esp_err_t open_socket(void)
{
    transport_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (transport_socket < 0) return ESP_FAIL;
    const int enabled = 1;
    setsockopt(transport_socket, SOL_SOCKET, SO_REUSEADDR,
               &enabled, sizeof(enabled));
    setsockopt(transport_socket, SOL_SOCKET, SO_BROADCAST,
               &enabled, sizeof(enabled));
    const struct sockaddr_in local_address = {
        .sin_family = AF_INET,
        .sin_port = htons(CLUSTER_WIRELESS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(transport_socket, (const struct sockaddr *)&local_address,
             sizeof(local_address)) != 0) {
        close(transport_socket);
        transport_socket = -1;
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void receive_task(void *argument)
{
    (void)argument;
    cluster_wireless_packet_t packet;
    while (true) {
        const int received = recv(transport_socket, &packet, sizeof(packet), 0);
        const size_t header_size = offsetof(cluster_wireless_packet_t, frame);
        const uint8_t allowed_sources =
            transport_config.role == CLUSTER_WIRELESS_ROLE_CENTER
                ? CLUSTER_WIRELESS_ROLE_LEFT | CLUSTER_WIRELESS_ROLE_RIGHT |
                      CLUSTER_WIRELESS_ROLE_KNOB
                : CLUSTER_WIRELESS_ROLE_CENTER;
        const uint16_t frame_size = ntohs(packet.frame_size);
        if (received < (int)header_size ||
            ntohs(packet.magic) != CLUSTER_WIRELESS_MAGIC ||
            packet.version != CLUSTER_WIRELESS_VERSION ||
            (packet.source & allowed_sources) == 0U ||
            (packet.source & (packet.source - 1U)) != 0U ||
            (packet.destination & transport_config.role) == 0U ||
            frame_size > CLUSTER_WIRELESS_MAX_FRAME_SIZE ||
            header_size + frame_size != (size_t)received) {
            continue;
        }
        cluster_wireless_queue_item_t item = {.size = frame_size};
        memcpy(item.frame, packet.frame, item.size);
        if (xQueueSend(receive_queue, &item, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Receive queue full");
        }
    }
}

static void initialization_task(void *argument)
{
    (void)argument;
    esp_err_t error = ESP_OK;
    esp_netif_t *station_netif = NULL;
    if (transport_config.initialize_wifi) {
        error = initialize_wifi(&station_netif);
    }
    if (error == ESP_OK && transport_config.connect_to_center) {
        error = connect_to_center(&station_netif);
    } else if (error == ESP_OK) {
        uint8_t channel = 0U;
        wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
        do {
            error = esp_wifi_get_channel(&channel, &secondary);
            if (error != ESP_OK || channel != CLUSTER_WIRELESS_CHANNEL) {
                vTaskDelay(pdMS_TO_TICKS(250));
            }
        } while (error != ESP_OK || channel != CLUSTER_WIRELESS_CHANNEL);
    }
    if (error == ESP_OK) error = open_socket();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Wireless initialization failed: %s",
                 esp_err_to_name(error));
        vTaskDelete(NULL);
        return;
    }
    atomic_store_explicit(&transport_ready, true, memory_order_release);
    ESP_LOGI(TAG, "Wireless cluster ready on channel %u UDP port %u",
             CLUSTER_WIRELESS_CHANNEL, CLUSTER_WIRELESS_PORT);
    if (xTaskCreate(receive_task, "cluster_wireless_rx", 4096, NULL, 6,
                    NULL) != pdPASS) {
        atomic_store_explicit(&transport_ready, false, memory_order_release);
        ESP_LOGE(TAG, "Receive task creation failed");
    }
    vTaskDelete(NULL);
}

esp_err_t cluster_wireless_start(const cluster_wireless_config_t *config)
{
    if (!config || config->role == 0U ||
        (config->role & (config->role - 1U)) != 0U || receive_queue) {
        return ESP_ERR_INVALID_ARG;
    }
    transport_config = *config;
    receive_queue = xQueueCreate(CLUSTER_WIRELESS_QUEUE_DEPTH,
                                 sizeof(cluster_wireless_queue_item_t));
    if (!receive_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(initialization_task, "cluster_wireless", 5120, NULL, 6,
                    NULL) != pdPASS) {
        vQueueDelete(receive_queue);
        receive_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool cluster_wireless_ready(void)
{
    return atomic_load_explicit(&transport_ready, memory_order_acquire);
}

esp_err_t cluster_wireless_send(cluster_wireless_role_t destination,
                                const void *frame, size_t frame_size)
{
    if (!frame || frame_size == 0U ||
        frame_size > CLUSTER_WIRELESS_MAX_FRAME_SIZE ||
        (destination & CLUSTER_WIRELESS_ROLE_ALL) == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!cluster_wireless_ready()) return ESP_ERR_INVALID_STATE;
    cluster_wireless_packet_t packet = {
        .magic = htons(CLUSTER_WIRELESS_MAGIC),
        .version = CLUSTER_WIRELESS_VERSION,
        .source = (uint8_t)transport_config.role,
        .destination = (uint8_t)destination,
        .frame_size = htons((uint16_t)frame_size),
    };
    memcpy(packet.frame, frame, frame_size);
    const struct sockaddr_in destination_address = {
        .sin_family = AF_INET,
        .sin_port = htons(CLUSTER_WIRELESS_PORT),
        .sin_addr.s_addr = inet_addr(
            destination == CLUSTER_WIRELESS_ROLE_CENTER
                ? "192.168.4.1" : "192.168.4.255"),
    };
    const size_t packet_size = offsetof(cluster_wireless_packet_t, frame) +
                               frame_size;
    const int sent = sendto(
        transport_socket, &packet, packet_size, 0,
        (const struct sockaddr *)&destination_address,
        sizeof(destination_address));
    return sent == (int)packet_size ? ESP_OK : ESP_FAIL;
}

esp_err_t cluster_wireless_receive(void *frame, size_t frame_capacity,
                                   size_t *frame_size, TickType_t timeout)
{
    if (!frame || !frame_size) return ESP_ERR_INVALID_ARG;
    cluster_wireless_queue_item_t item;
    if (xQueueReceive(receive_queue, &item, timeout) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (item.size > frame_capacity) return ESP_ERR_INVALID_SIZE;
    memcpy(frame, item.frame, item.size);
    *frame_size = item.size;
    return ESP_OK;
}

esp_err_t cluster_wireless_send_message(cluster_wireless_role_t destination,
                                        cluster_message_type_t type,
                                        const void *payload,
                                        size_t payload_size)
{
    if (!cluster_wireless_ready()) return ESP_ERR_INVALID_STATE;
    uint8_t frame[CLUSTER_PROTOCOL_MAX_FRAME_SIZE];
    size_t frame_size = 0U;
    if (payload_size > CLUSTER_PROTOCOL_MAX_PAYLOAD ||
        cluster_frame_encode(
            type,
            (uint16_t)atomic_fetch_add_explicit(&message_sequence, 1U,
                                                memory_order_relaxed),
            (uint32_t)(esp_timer_get_time() / 1000), payload,
            (uint16_t)payload_size, frame, sizeof(frame), &frame_size) !=
            CLUSTER_PROTOCOL_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return cluster_wireless_send(destination, frame, frame_size);
}

esp_err_t cluster_wireless_receive_message(uint8_t *buffer, size_t capacity,
                                           size_t *frame_size,
                                           cluster_frame_view_t *view,
                                           TickType_t timeout)
{
    if (!frame_size || !view) return ESP_ERR_INVALID_ARG;
    const esp_err_t error =
        cluster_wireless_receive(buffer, capacity, frame_size, timeout);
    if (error != ESP_OK) return error;
    return cluster_frame_decode(buffer, *frame_size, view) ==
                   CLUSTER_PROTOCOL_OK
               ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}
