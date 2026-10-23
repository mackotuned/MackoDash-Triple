#include "dns_server.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#define DNS_PORT 53
#define DNS_MAX_PACKET_LEN 512
#define DNS_QUESTION_TYPE_A 1U
#define DNS_QUESTION_CLASS_IN 1U
#define DNS_RESPONSE_FLAGS 0x8180U
#define DNS_NXDOMAIN_FLAGS 0x8183U
#define DNS_MAX_NAME_LEN 256
#define DNS_ANSWER_TTL_SECONDS 60U

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t question_count;
    uint16_t answer_count;
    uint16_t authority_count;
    uint16_t additional_count;
} dns_header_t;

typedef struct __attribute__((packed)) {
    uint16_t name_offset;
    uint16_t type;
    uint16_t class_code;
    uint32_t ttl;
    uint16_t address_length;
    uint32_t address;
} dns_answer_t;

struct dns_server_handle {
    bool started;
    TaskHandle_t task;
    int entry_count;
    dns_entry_pair_t entries[];
};

static const char *TAG = "captive_dns";

static const uint8_t *skip_dns_name(const uint8_t *cursor, const uint8_t *end)
{
    while (cursor < end && *cursor != 0U) {
        uint8_t label_length = *cursor++;
        if ((label_length & 0xC0U) != 0U || label_length > 63U ||
            (size_t)(end - cursor) < label_length) {
            return NULL;
        }
        cursor += label_length;
    }
    return cursor < end ? cursor + 1 : NULL;
}

/* Copies the queried name as lowercase "label.label" into out. */
static bool read_dns_name(const uint8_t *cursor, const uint8_t *end,
                          char *out, size_t capacity)
{
    size_t used = 0;
    while (cursor < end && *cursor != 0U) {
        uint8_t label_length = *cursor++;
        if ((label_length & 0xC0U) != 0U || (size_t)(end - cursor) < label_length ||
            used + label_length + 2U > capacity) {
            return false;
        }
        if (used > 0) out[used++] = '.';
        for (uint8_t index = 0; index < label_length; ++index) {
            char c = (char)cursor[index];
            out[used++] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        }
        cursor += label_length;
    }
    out[used] = '\0';
    return true;
}

static bool entry_matches(const dns_entry_pair_t *entry, const char *name)
{
    return !entry->name || strcmp(entry->name, "*") == 0 ||
           strcasecmp(entry->name, name) == 0;
}

/* Returns the address for the first entry whose name matches, IPADDR_ANY if
 * it matches but the interface has no address yet, or IPADDR_NONE when no
 * entry matches (answered with NXDOMAIN). */
static uint32_t response_address(const dns_server_handle_t handle, const char *name)
{
    for (int index = 0; index < handle->entry_count; ++index) {
        if (!entry_matches(&handle->entries[index], name)) continue;
        if (handle->entries[index].if_key) {
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey(
                handle->entries[index].if_key);
            esp_netif_ip_info_t ip_info;
            if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                return ip_info.ip.addr;
            }
        } else if (handle->entries[index].ip.addr != IPADDR_ANY) {
            return handle->entries[index].ip.addr;
        }
        return IPADDR_ANY;
    }
    return IPADDR_NONE;
}

static int prepare_response(const uint8_t *request, size_t request_length,
                            uint8_t *response, size_t response_capacity,
                            dns_server_handle_t handle)
{
    if (request_length < sizeof(dns_header_t) ||
        request_length > response_capacity - sizeof(dns_answer_t)) {
        return -1;
    }

    memcpy(response, request, request_length);
    dns_header_t *header = (dns_header_t *)response;
    if (ntohs(header->question_count) != 1U) return -1;

    const uint8_t *question = skip_dns_name(
        request + sizeof(dns_header_t), request + request_length);
    if (!question || (size_t)((request + request_length) - question) < 4U) {
        return -1;
    }

    uint16_t question_type;
    uint16_t question_class;
    memcpy(&question_type, question, sizeof(question_type));
    memcpy(&question_class, question + sizeof(question_type),
           sizeof(question_class));
    char name[DNS_MAX_NAME_LEN];
    if (!read_dns_name(request + sizeof(dns_header_t), question, name, sizeof(name))) {
        return -1;
    }
    uint32_t address = response_address(handle, name);

    header->authority_count = 0;
    header->additional_count = 0;
    if (address == IPADDR_NONE) {
        /* Not one of our names: answer "no such domain" straight away so
         * phones conclude there is no Internet here instead of a portal. */
        header->flags = htons(DNS_NXDOMAIN_FLAGS);
        header->answer_count = 0;
        return (int)request_length;
    }
    header->flags = htons(DNS_RESPONSE_FLAGS);
    if (ntohs(question_type) != DNS_QUESTION_TYPE_A ||
        ntohs(question_class) != DNS_QUESTION_CLASS_IN ||
        address == IPADDR_ANY) {
        header->answer_count = 0;
        return (int)request_length;
    }

    header->answer_count = htons(1U);
    dns_answer_t answer = {
        .name_offset = htons(0xC00CU),
        .type = htons(DNS_QUESTION_TYPE_A),
        .class_code = htons(DNS_QUESTION_CLASS_IN),
        .ttl = htonl(DNS_ANSWER_TTL_SECONDS),
        .address_length = htons(sizeof(address)),
        .address = address,
    };
    memcpy(response + request_length, &answer, sizeof(answer));
    return (int)(request_length + sizeof(answer));
}

static void dns_server_task(void *argument)
{
    dns_server_handle_t handle = argument;
    struct sockaddr_in bind_address = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (socket_fd < 0 ||
        bind(socket_fd, (struct sockaddr *)&bind_address,
             sizeof(bind_address)) < 0) {
        ESP_LOGE(TAG, "DNS socket setup failed: errno %d", errno);
        if (socket_fd >= 0) close(socket_fd);
        handle->started = false;
        handle->task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS ready on UDP port %d (%s)", DNS_PORT,
             handle->entry_count == 1 && handle->entries[0].name &&
             strcmp(handle->entries[0].name, "*") == 0 ? "wildcard" : "named");
    uint8_t request[DNS_MAX_PACKET_LEN];
    uint8_t response[DNS_MAX_PACKET_LEN];
    while (handle->started) {
        struct sockaddr_storage source;
        socklen_t source_length = sizeof(source);
        int received = recvfrom(socket_fd, request, sizeof(request), 0,
                                (struct sockaddr *)&source, &source_length);
        if (received < 0) {
            if (handle->started) {
                ESP_LOGE(TAG, "DNS receive failed: errno %d", errno);
            }
            break;
        }
        int response_length = prepare_response(
            request, (size_t)received, response, sizeof(response), handle);
        if (response_length > 0) {
            sendto(socket_fd, response, (size_t)response_length, 0,
                   (struct sockaddr *)&source, source_length);
        }
    }

    shutdown(socket_fd, 0);
    close(socket_fd);
    handle->task = NULL;
    vTaskDelete(NULL);
}

dns_server_handle_t start_dns_server(const dns_server_config_t *config)
{
    ESP_RETURN_ON_FALSE(config && config->num_of_entries > 0 &&
                            config->num_of_entries <= DNS_SERVER_MAX_ITEMS,
                        NULL, TAG, "Invalid DNS configuration");
    size_t allocation_size = sizeof(struct dns_server_handle) +
                             config->num_of_entries * sizeof(dns_entry_pair_t);
    dns_server_handle_t handle = calloc(1, allocation_size);
    ESP_RETURN_ON_FALSE(handle, NULL, TAG, "DNS allocation failed");
    handle->started = true;
    handle->entry_count = config->num_of_entries;
    memcpy(handle->entries, config->item,
           config->num_of_entries * sizeof(dns_entry_pair_t));
    if (xTaskCreate(dns_server_task, "captive_dns", 4096, handle, 5,
                    &handle->task) != pdPASS) {
        free(handle);
        return NULL;
    }
    return handle;
}

void stop_dns_server(dns_server_handle_t handle)
{
    if (!handle) return;
    handle->started = false;
    if (handle->task) vTaskDelete(handle->task);
    free(handle);
}