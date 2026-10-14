idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES cluster_protocol vehicle_data settings_schema esp_timer
)
