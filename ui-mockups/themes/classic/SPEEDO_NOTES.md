# EK Civic SiR speedometer: 480x480 round, LVGL instructions

Image: `speedo_480_round.png`, 480x480. The centre is at (240, 240). It uses the same face, tick style, font, white and red (`#E81E1E`) as the tach.
The corners outside the circle are black. The needle and centre cap are **not** drawn on the image.

What's on the face:
- **Main scale:** 0–160 mph in white. Numbers every 20, ticks every 5.
- **Inner scale:** km/h in red, numbers every 40 (0–240). It's placed to line up exactly with the mph scale.

## Needle geometry (same sweep as the tach)
| mph | angle from 12 o'clock (clockwise) | LVGL angle (from 3 o'clock, clockwise) |
|---|---|---|
| 0   | -130° (about 7:30) | 140° |
| 80  | 0° (straight up)   | 270° |
| 160 | +130° (about 4:30) | 40° (i.e. 400°) |

- Sweep is 260°, which is **1.625° per mph**.
- Needle length is about **218 px**; the tick ring spans 197 to 226 px.
- Centre cap is about **30 px** radius, colour `#0E0E0F`. Use the same needle colour as the tach so they match.

```c
float deg_from_12 = -130.0f + 260.0f * mph / 160.0f;
int16_t lv_angle  = (int16_t)(deg_from_12 + 270.0f);
```

## LVGL v8 (lv_meter)
```c
LV_IMG_DECLARE(speedo_480_round);

lv_obj_t *bg = lv_img_create(lv_scr_act());
lv_img_set_src(bg, &speedo_480_round);
lv_obj_center(bg);

lv_obj_t *meter = lv_meter_create(lv_scr_act());
lv_obj_set_size(meter, 480, 480);
lv_obj_center(meter);
lv_obj_set_style_bg_opa(meter, LV_OPA_TRANSP, 0);
lv_obj_set_style_border_width(meter, 0, 0);
lv_obj_set_style_pad_all(meter, 0, 0);
lv_obj_set_style_size(meter, 0, LV_PART_INDICATOR);           /* hide built-in hub dot */

lv_meter_scale_t *scale = lv_meter_add_scale(meter);
lv_meter_set_scale_ticks(meter, scale, 0, 0, 0, lv_color_black());  /* ticks are in the PNG */
lv_meter_set_scale_range(meter, scale, 0, 160, 260, 140);

/* r_mod = -22 -> needle length 240-22 = 218 px */
lv_meter_indicator_t *needle =
    lv_meter_add_needle_line(meter, scale, 5, lv_color_hex(0xEC5C28), -22);

lv_obj_t *hub = lv_obj_create(lv_scr_act());
lv_obj_remove_style_all(hub);
lv_obj_set_size(hub, 60, 60);
lv_obj_center(hub);
lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
lv_obj_set_style_bg_color(hub, lv_color_hex(0x0E0E0F), 0);

/* update loop */
lv_meter_set_indicator_value(meter, needle, mph);   /* 0..160 */
```

## LVGL v9 (lv_scale)
```c
lv_obj_t *scale = lv_scale_create(lv_screen_active());
lv_obj_set_size(scale, 480, 480);
lv_obj_center(scale);
lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
lv_scale_set_range(scale, 0, 160);
lv_scale_set_angle_range(scale, 260);
lv_scale_set_rotation(scale, 140);
lv_scale_set_total_tick_count(scale, 0);
lv_scale_set_label_show(scale, false);
lv_obj_set_style_bg_opa(scale, LV_OPA_TRANSP, 0);
lv_obj_set_style_arc_width(scale, 0, LV_PART_MAIN);

lv_obj_t *needle = lv_line_create(scale);
lv_obj_set_style_line_width(needle, 5, 0);
lv_obj_set_style_line_color(needle, lv_color_hex(0xEC5C28), 0);
lv_obj_set_style_line_rounded(needle, true, 0);

/* update loop */
lv_scale_set_line_needle_value(scale, needle, 218, mph);
```

## Converting the PNG
Use the same converter as for the other screens: RGB565, C array. 480x480 RGB565 is 460 KB.

## Tweaking
Constants are at the top of `make_speedo.py`:
- `MPH_MAX` sets the top of the scale. If you change it, change the LVGL range to match.
- `SHOW_KMH = False` removes the red inner scale.

Run `python make_speedo.py` to regenerate the PNG, the preview and `speedo_layout.json`.
