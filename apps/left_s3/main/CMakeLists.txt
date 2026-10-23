idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES climate_link cluster_board_config cluster_protocol cluster_secrets
             cluster_wireless side_display side_pages esp_timer
)
