idf_component_register(
    SRCS "cluster_ota.c"
    INCLUDE_DIRS "include"
    REQUIRES app_update cluster_protocol esp_app_format esp_http_client
             esp_https_ota
)
