file(GLOB KNOB_ASSETS RELATIVE "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/assets/rgb565z/*.z")
list(SORT KNOB_ASSETS)

idf_component_register(SRCS "knob_app.c"
                   INCLUDE_DIRS "."
                   EMBED_FILES ${KNOB_ASSETS}
            REQUIRES cluster_board_config cluster_ota cluster_protocol cluster_wireless settings_schema
                          esp_driver_gpio esp_driver_i2c esp_driver_ledc
                          esp_driver_spi esp_lcd esp_rom esp_timer)