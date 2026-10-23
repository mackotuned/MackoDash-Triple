# EK Civic SiR tach face: LVGL overlay notes

Image: `tach_ek_800.png`, 800x800. The centre is at (400, 400). The face runs edge to edge with no bezel; the corners outside the circle are black. The needle and centre cap are not drawn on it.

## Needle geometry
| rpm | angle from 12 o'clock (clockwise) | LVGL angle (from 3 o'clock, clockwise) |
|-----|-----|-----|
| 0     | -130° (about 7:30) | 140° |
| 4500  | 0° (straight up)   | 270° |
| 8000  | +101.1° (red band starts) | 11.1° |
| 9000  | +130° (about 4:30) | 40° (i.e. 400°) |

- Sweep is 260°, which is **28.89° per 1000 rpm**.
- The red band runs from 8000 to about 9250 rpm. Allow the needle to reach 9250 if you want it to bury into the end of the band.
- Needle tip radius is about **363 px** (just inside the tick ring, which spans 340 to 376 px).
- Centre cap is about **50 px** radius, colour `#0E0E0F`. The OEM needle is orange-red, about `#EC5C28`.
- The pin holes at (241, 404) and (570, 404) are part of the face. Keep the hub smaller than about 140 px radius so it doesn't cover them.

Formula (any LVGL version):
```c
float deg_from_12 = -130.0f + 260.0f * rpm / 9000.0f;   /* 0 = up, clockwise + */
int16_t lv_angle  = (int16_t)(deg_from_12 + 270.0f);    /* LVGL: 0 = 3 o'clock */
```

## LVGL v8 (lv_meter)
```c
LV_IMG_DECLARE(tach_ek_800);

lv_obj_t *bg = lv_img_create(lv_scr_act());
lv_img_set_src(bg, &tach_ek_800);
lv_obj_center(bg);

lv_obj_t *meter = lv_meter_create(lv_scr_act());
lv_obj_set_size(meter, 800, 800);
lv_obj_center(meter);
lv_obj_set_style_bg_opa(meter, LV_OPA_TRANSP, 0);
lv_obj_set_style_border_width(meter, 0, 0);
lv_obj_set_style_pad_all(meter, 0, 0);
lv_obj_set_style_size(meter, 0, LV_PART_INDICATOR);          /* hide built-in hub dot */

lv_meter_scale_t *scale = lv_meter_add_scale(meter);
lv_meter_set_scale_ticks(meter, scale, 0, 0, 0, lv_color_black()); /* ticks are in the PNG */
lv_meter_set_scale_range(meter, scale, 0, 9000, 260, 140);

/* r_mod = -37 -> needle length 400-37 = 363 px */
lv_meter_indicator_t *needle =
    lv_meter_add_needle_line(meter, scale, 7, lv_color_hex(0xEC5C28), -37);

lv_obj_t *hub = lv_obj_create(lv_scr_act());
lv_obj_remove_style_all(hub);
lv_obj_set_size(hub, 100, 100);
lv_obj_center(hub);
lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
lv_obj_set_style_bg_color(hub, lv_color_hex(0x0E0E0F), 0);

/* update loop */
lv_meter_set_indicator_value(meter, needle, rpm);   /* 0..9000 (or 9250) */
```

## LVGL v9 (lv_scale)
```c
lv_obj_t *scale = lv_scale_create(lv_screen_active());
lv_obj_set_size(scale, 800, 800);
lv_obj_center(scale);
lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
lv_scale_set_range(scale, 0, 9000);
lv_scale_set_angle_range(scale, 260);
lv_scale_set_rotation(scale, 140);
lv_scale_set_total_tick_count(scale, 0);
lv_scale_set_label_show(scale, false);
lv_obj_set_style_bg_opa(scale, LV_OPA_TRANSP, 0);
lv_obj_set_style_arc_width(scale, 0, LV_PART_MAIN);

lv_obj_t *needle = lv_line_create(scale);
lv_obj_set_style_line_width(needle, 7, 0);
lv_obj_set_style_line_color(needle, lv_color_hex(0xEC5C28), 0);
lv_obj_set_style_line_rounded(needle, true, 0);

/* update loop */
lv_scale_set_line_needle_value(scale, needle, 363, rpm);
```

## Converting the PNG
- **v8:** lvgl.io/tools/imageconverter, colour format "CF_TRUE_COLOR" (RGB565), output C array.
- **v9:** `scripts/LVGLImage.py --cf RGB565 --ofmt C tach_ek_800.png` from the LVGL repo.

At RGB565, 800x800 is 1.28 MB. Keep it in flash or PSRAM, or load it from LittleFS or SD at runtime.
If your panel is RGB565 and you see banding in the dark areas, enable dithering in the converter.

## Tweaking
All geometry and colours are constants at the top of `make_tach.py`. Run `python make_tach.py`
to regenerate. It needs Pillow only. The script uses the Microgramma D Extended Bold font installed
on this PC and falls back to Bahnschrift if that font isn't there.
