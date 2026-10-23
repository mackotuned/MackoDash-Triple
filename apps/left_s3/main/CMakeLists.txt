idf_component_register(
    SRCS "main.c" "left_dashboard.c"
    INCLUDE_DIRS "."
    REQUIRES climate_display climate_link cluster_secrets cluster_board_config cluster_protocol cluster_wireless dash_fonts side_display vehicle_data settings_schema esp_timer
    EMBED_FILES "assets/side_480_rgb565.bin"
        "assets/graphite_side_480_rgb565.bin"
)
