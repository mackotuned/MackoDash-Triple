idf_component_register(
    SRCS "side_display.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_ledc
        esp_io_expander_tca9554
        esp_lcd
        esp_lcd_panel_io_additions
        esp_lcd_st7701
        esp_lvgl_adapter
        lvgl
)