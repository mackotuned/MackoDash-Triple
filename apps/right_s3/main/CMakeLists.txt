idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES air_ride_gateway cluster_board_config cluster_protocol cluster_wireless
             side_display side_pages esp_timer
)
