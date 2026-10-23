#include "climate_display.h"

#include <stdint.h>

extern const uint8_t climate_480_rgb565_start[]
    asm("_binary_climate_480_rgb565_bin_start");
extern const uint8_t graphite_climate_480_rgb565_start[]
    asm("_binary_graphite_climate_480_rgb565_bin_start");

static lv_obj_t *offline_overlay;
static lv_obj_t *climate_background;
static lv_image_dsc_t climate_image = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = 480,
        .h = 480,
        .stride = 960,
    },
    .data_size = 480U * 480U * 2U,
    .data = climate_480_rgb565_start,
};

lv_obj_t *climate_display_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    climate_background = lv_image_create(screen);
    lv_image_set_src(climate_background, &climate_image);
    lv_obj_center(climate_background);
    lv_obj_set_clickable(climate_background, false);

    offline_overlay = lv_obj_create(screen);
    lv_obj_set_size(offline_overlay, 472, 472);
    lv_obj_center(offline_overlay);
    lv_obj_set_style_radius(offline_overlay, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(offline_overlay, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(offline_overlay, 6, LV_PART_MAIN);
    lv_obj_set_style_border_color(offline_overlay, lv_color_hex(0xFF4D3D),
                                  LV_PART_MAIN);
    lv_obj_set_clickable(offline_overlay, false);

    lv_obj_t *status = lv_label_create(offline_overlay);
    lv_label_set_text(status, "CENTER OFFLINE");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(status, lv_color_hex(0xFF4D3D), LV_PART_MAIN);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 112);
    return screen;
}

void climate_display_set_graphite(bool graphite)
{
    climate_image.data = graphite ? graphite_climate_480_rgb565_start
                                  : climate_480_rgb565_start;
    lv_image_set_src(climate_background, &climate_image);
    lv_obj_invalidate(climate_background);
}

void climate_display_set_offline(bool offline)
{
    if (offline_overlay != NULL) lv_obj_set_hidden(offline_overlay, !offline);
}
