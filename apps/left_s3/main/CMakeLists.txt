idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES cluster_board_config cluster_protocol cluster_uart vehicle_data settings_schema esp_timer
)
