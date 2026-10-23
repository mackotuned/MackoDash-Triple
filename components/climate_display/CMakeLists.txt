idf_component_register(
    SRCS "climate_display.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl
    EMBED_FILES "assets/climate_480_rgb565.bin"
        "assets/graphite_climate_480_rgb565.bin"
)
