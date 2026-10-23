idf_component_register(
    SRCS "side_display.c" "side_speedometer.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_ledc
        esp_io_expander_tca9554
        esp_lcd
        esp_lcd_panel_io_additions
        esp_lcd_st7701
        esp_lcd_touch_gt911
        esp_lvgl_adapter
        lvgl
        vehicle_data
    EMBED_FILES "assets/speedo_480_rgb565.bin"
        "assets/graphite_speedo_480_rgb565.bin"
)