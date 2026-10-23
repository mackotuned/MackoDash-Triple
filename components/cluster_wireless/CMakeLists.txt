idf_component_register(
    SRCS "cluster_wireless.c"
    INCLUDE_DIRS "include"
    REQUIRES cluster_protocol cluster_secrets esp_event esp_timer esp_netif esp_wifi lwip nvs_flash
)