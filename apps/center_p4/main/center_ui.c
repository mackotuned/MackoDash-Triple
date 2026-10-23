#include "center_ui.h"

#include <string.h>

#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "src/image/lv_image_decoder_private.h"
#include "src/misc/cache/instance/lv_image_cache.h"

static const char *TAG = "center_ui";

lv_obj_t *center_ui_label_create(lv_obj_t *parent, const char *text,
                                 const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    return label;
}

void center_ui_label_set_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

lv_obj_t *center_ui_panel_create(lv_obj_t *screen, uint32_t color)
{
    lv_obj_t *panel = lv_obj_create(screen);
    lv_obj_set_size(panel, 800, 800);
    lv_obj_center(panel);
    lv_obj_set_style_radius(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(panel, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(panel, false);
    lv_obj_set_hidden(panel, true);
    return panel;
}

void center_ui_png_init(lv_image_dsc_t *descriptor, const uint8_t *start,
                        const uint8_t *end, uint32_t width, uint32_t height)
{
    *descriptor = (lv_image_dsc_t){
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RAW,
            .w = width,
            .h = height,
        },
        .data_size = (uint32_t)(end - start),
        .data = start,
    };
}

/* The PNG decoder produces ARGB8888, which LVGL alpha-blends on every
 * redraw. RGB565 redraws are plain copies the PPA can do in hardware. */
void center_ui_flatten_to_rgb565(lv_image_dsc_t *descriptor)
{
    lv_image_decoder_dsc_t decoder;
    if (lv_image_decoder_open(&decoder, descriptor, NULL) != LV_RESULT_OK) {
        ESP_LOGW(TAG, "Image decode failed; keeping PNG source");
        return;
    }
    const lv_draw_buf_t *decoded = decoder.decoded;
    if (decoded == NULL || decoded->header.cf != LV_COLOR_FORMAT_ARGB8888) {
        lv_image_decoder_close(&decoder);
        return;
    }

    const uint32_t width = decoded->header.w;
    const uint32_t height = decoded->header.h;
    const uint32_t stride = width * 2U;
    const size_t size = ((size_t)stride * height + 127U) & ~(size_t)127U;
    uint8_t *pixels = heap_caps_aligned_alloc(128, size, MALLOC_CAP_SPIRAM);
    if (pixels == NULL) {
        lv_image_decoder_close(&decoder);
        ESP_LOGW(TAG, "No PSRAM for RGB565 image; keeping PNG source");
        return;
    }
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t *source = decoded->data + y * decoded->header.stride;
        uint16_t *destination = (uint16_t *)(pixels + y * stride);
        for (uint32_t x = 0; x < width; ++x, source += 4) {
            /* ARGB8888 is stored B, G, R, A. */
            destination[x] = (uint16_t)(((source[2] & 0xF8U) << 8) |
                                        ((source[1] & 0xFCU) << 3) |
                                        (source[0] >> 3));
        }
    }
    lv_image_decoder_close(&decoder);
    lv_image_cache_drop(descriptor);
    esp_cache_msync(pixels, size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    *descriptor = (lv_image_dsc_t){
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .w = width,
            .h = height,
            .stride = stride,
        },
        .data_size = stride * height,
        .data = pixels,
    };
}

void center_ui_theme_image_set_active(lv_obj_t *image, bool active)
{
    lv_obj_set_hidden(image, !active);
    if (active) {
        lv_obj_remove_state(image, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(image, LV_STATE_DISABLED);
    }
}
