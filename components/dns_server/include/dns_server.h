#pragma once

#include "esp_netif_ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DNS_SERVER_MAX_ITEMS
#define DNS_SERVER_MAX_ITEMS 2
#endif

/* Entry names are matched case-insensitively against the queried name;
 * "*" matches every name (captive-portal behaviour). Queries that match no
 * entry are answered with NXDOMAIN. */
#define DNS_SERVER_CONFIG_SINGLE(queried_name, netif_key) { \
    .num_of_entries = 1,                                     \
    .item = {{.name = queried_name, .if_key = netif_key}},   \
}

#define DNS_SERVER_CONFIG_PAIR(name_a, name_b, netif_key) {  \
    .num_of_entries = 2,                                     \
    .item = {{.name = name_a, .if_key = netif_key},          \
             {.name = name_b, .if_key = netif_key}},         \
}

typedef struct {
    const char *name;
    const char *if_key;
    esp_ip4_addr_t ip;
} dns_entry_pair_t;

typedef struct {
    int num_of_entries;
    dns_entry_pair_t item[DNS_SERVER_MAX_ITEMS];
} dns_server_config_t;

typedef struct dns_server_handle *dns_server_handle_t;

dns_server_handle_t start_dns_server(const dns_server_config_t *config);
void stop_dns_server(dns_server_handle_t handle);

#ifdef __cplusplus
}
#endif