idf_component_register(
    SRCS "cluster_uart.c"
    INCLUDE_DIRS "include"
    REQUIRES cluster_protocol esp_driver_uart
)