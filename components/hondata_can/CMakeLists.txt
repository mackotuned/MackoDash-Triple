idf_component_register(
    SRCS "hondata_can.c" "hondata_can_runtime.c"
    INCLUDE_DIRS "include"
    REQUIRES cluster_board_config driver esp_driver_twai esp_timer vehicle_data
)