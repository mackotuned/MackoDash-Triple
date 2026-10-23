# EK Civic SiR side screen: 480x480 round, LVGL instructions

Image: `side_480_round.png`, 480x480. The centre is at (240, 240). It uses the same textured face, font and colours as the tach.

What it shows:
- **Boost (psi):** big number in the centre.
- **Air temp (°F):** small number, bottom left.
- **Battery (V):** small number, bottom right.
- **Fuel:** left edge, 8 segments. E is at the bottom and F at the top. Segment 0 is reserve.
- **Water temp:** right edge, 10 segments, C at the bottom and H at the top. **This scale is not linear** (see below). The top 2 segments are the red warning zone.

Labels, units, icons, ticks and the dark (unlit) segments are printed on the image.
LVGL draws only **3 numbers** and **the lit segments**. Everything is also in `side_layout.json`.

## Number boxes (screen px, centre-aligned)
| Key | x | y | w | h | Digit height | Suggested font px |
|---|---|---|---|---|---|---|
| boost   | 140 | 166 | 200 | 70 | 50 | ~72 |
| iat     | 126 | 295 | 104 | 36 | 24 | ~35 |
| battery | 250 | 295 | 104 | 36 | 24 | ~35 |

**Font:** to match the printed text, convert *Microgramma D Extended Bold* (it's on your PC) with LVGL's font converter
(lvgl.io/tools/fontconverter). Include only the characters `0123456789.-` (the `-` is for boost vacuum) and make two
sizes, 72 px and 35 px. Microgramma is a commercial font, so check its licence before you share the firmware.

## Water temp scale (non-linear)
| Temp | Segments lit | Where on the arc |
|---|---|---|
| 60 °F or below | 0 | bottom (C) |
| 190 °F (normal running) | 5 | **middle tick** |
| about 209 °F and up | 8–10 | red warning zone |
| 225 °F or above | 10 | top (H) |

It's two straight sections: 60→190 °F fills the bottom half (26 °F per segment), and 190→225 °F fills
the top half (7 °F per segment). So a warm engine sits on the middle tick, and anything hotter than
normal moves the bar quickly.

## Arc segments (LVGL degrees, where 0 = 3 o'clock, clockwise; segment 0 = bottom)
Both arcs have an outer radius of **226 px** (so the object is 452x452) and a width of **16 px**.
```c
static const seg_t FUEL_SEG[8] = {
  {128,139.07},{141.28,152.35},{154.55,165.62},{167.82,178.9},
  {181.1,192.18},{194.38,205.45},{207.65,218.72},{220.92,232}};
static const seg_t WATER_SEG[10] = {
  {43.58,52},{32.96,41.38},{22.34,30.76},{11.72,20.14},{1.1,9.52},
  {350.48,358.9},{339.86,348.28},{329.24,337.66},{318.62,327.04},{308,316.42}};
```

## LVGL code (v8; see below for v9 renames)
```c
#include "lvgl.h"
#include <stdio.h>

LV_IMG_DECLARE(side_480_round);
LV_FONT_DECLARE(micro_72);
LV_FONT_DECLARE(micro_35);

#define WHITE  lv_color_hex(0xECECE7)
#define RED    lv_color_hex(0xE81E1E)

typedef struct { float start, end; } seg_t;
/* FUEL_SEG and WATER_SEG from above */

static lv_obj_t *fuel_arc[8], *water_arc[10];
static lv_obj_t *lbl_boost, *lbl_iat, *lbl_batt;

static lv_obj_t *make_seg(lv_obj_t *parent, seg_t s, lv_color_t col)
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_set_size(a, 452, 452);
    lv_obj_center(a);
    lv_arc_set_bg_angles(a, s.start, s.end);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_remove_style(a, NULL, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(a, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, false, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, col, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(a, 0, 0);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);           /* unlit: the dark segment shows from the PNG */
    return a;
}

static lv_obj_t *make_value(lv_obj_t *parent, int x, int y, int w, int h, const lv_font_t *f)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, w, h);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, WHITE, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, "--");
    return l;
}

void side_screen_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *bg = lv_img_create(scr);
    lv_img_set_src(bg, &side_480_round);
    lv_obj_set_pos(bg, 0, 0);

    for (int i = 0; i < 8; i++)  fuel_arc[i]  = make_seg(scr, FUEL_SEG[i], WHITE);
    for (int i = 0; i < 10; i++) water_arc[i] = make_seg(scr, WATER_SEG[i], i >= 8 ? RED : WHITE);

    /* boxes a little taller than the PNG boxes so the font's line height fits;
       if the digits sit off-centre vertically, nudge y by a few px */
    lbl_boost = make_value(scr, 140, 160, 200, 84, &micro_72);
    lbl_iat   = make_value(scr, 126, 290, 104, 44, &micro_35);
    lbl_batt  = make_value(scr, 250, 290, 104, 44, &micro_35);
}

static void light(lv_obj_t **segs, int n, int lit)
{
    for (int i = 0; i < n; i++) {
        if (i < lit) lv_obj_clear_flag(segs[i], LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(segs[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* Non-linear water temp: 60F -> 0, 190F -> 5 (middle tick), 225F -> 10 segments */
static int water_segments(float t)
{
    float s;
    if (t <= 60.0f)       s = 0.0f;
    else if (t <= 190.0f) s = 5.0f * (t - 60.0f) / 130.0f;
    else                  s = 5.0f + 5.0f * (t - 190.0f) / 35.0f;
    int n = (int)(s + 0.5f);              /* round, so 185-195F shows 5 */
    return n > 10 ? 10 : n;
}

/* call from your update timer */
void side_screen_update(float boost_psi, int iat_f, float volts, float water_f, int fuel_pct)
{
    char b[12];
    snprintf(b, sizeof b, "%.1f", boost_psi);
    lv_label_set_text(lbl_boost, b);
    lv_label_set_text_fmt(lbl_iat, "%d", iat_f);
    snprintf(b, sizeof b, "%.1f", volts);
    lv_label_set_text(lbl_batt, b);

    light(water_arc, 10, water_segments(water_f));

    /* fuel: 0..100 % over 8 segments; any fuel keeps at least 1 lit; last one goes red */
    int f = (fuel_pct * 8 + 99) / 100;  if (f > 8) f = 8;
    light(fuel_arc, 8, f);
    lv_obj_set_style_arc_color(fuel_arc[0], f <= 1 ? RED : WHITE, LV_PART_MAIN);

    lv_obj_set_style_text_color(lbl_boost, boost_psi >= 16.0f ? RED : WHITE, 0);
    lv_obj_set_style_text_color(lbl_batt, (volts < 12.0f || volts > 15.0f) ? RED : WHITE, 0);
}
```
**v9 renames:** `lv_scr_act` becomes `lv_screen_active`, `lv_img_*` becomes `lv_image_*`, `LV_IMG_DECLARE` becomes `LV_IMAGE_DECLARE`,
and `lv_obj_clear_flag` becomes `lv_obj_remove_flag`.
The warning levels (16 psi boost, 12–15 V battery) are placeholders, so set them for your car.

## Converting the PNG
Use the same converter as for the tach: RGB565, C array. 480x480 RGB565 is 460 KB.

## Tweaking
Layout constants are in `make_side.py`. Run `python make_side.py` to regenerate the PNG, the preview and `side_layout.json`.
