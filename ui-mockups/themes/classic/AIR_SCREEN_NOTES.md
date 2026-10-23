# Air ride screen: 480x480 round, LVGL instructions

Image: `air_480_round.png`, 480x480. The centre is at (240, 240). It uses the same textured face, font and colours as the tach and side screen.

What it shows:
- **Tank pressure:** big number in the centre.
- **Four corners:** FL and FR at the top, RL and RR at the bottom. FRONT and REAR markers show which way is forward.

The labels, units and markers are printed on the image. LVGL draws only **5 numbers**, all centre-aligned. The same numbers are in `air_layout.json`.

## Number boxes (screen px)
| Key | x | y | w | h | Digit height | Suggested font px |
|---|---|---|---|---|---|---|
| tank | 140 | 204 | 200 | 62 | 44 | ~64 |
| fl   |  91 | 126 | 110 | 40 | 28 | ~41 |
| fr   | 279 | 126 | 110 | 40 | 28 | ~41 |
| rl   |  91 | 314 | 110 | 40 | 28 | ~41 |
| rr   | 279 | 314 | 110 | 40 | 28 | ~41 |

**Font:** use the same converted Microgramma fonts as the side screen (characters `0123456789` only), in two sizes: 64 px and 41 px.
The side screen's 72 px and 35 px fonts also work if you want to save flash, but the numbers won't fill the boxes as neatly.

## LVGL code (v8; v9 renames are the same as in the other notes)
```c
#include "lvgl.h"

LV_IMG_DECLARE(air_480_round);
LV_FONT_DECLARE(micro_64);
LV_FONT_DECLARE(micro_41);

#define WHITE  lv_color_hex(0xECECE7)
#define RED    lv_color_hex(0xE81E1E)

static lv_obj_t *lbl_tank, *lbl_fl, *lbl_fr, *lbl_rl, *lbl_rr;

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

void air_screen_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *bg = lv_img_create(scr);
    lv_img_set_src(bg, &air_480_round);
    lv_obj_set_pos(bg, 0, 0);

    /* boxes a little taller than the PNG boxes so the font's line height fits;
       nudge y a few px if the digits sit off-centre */
    lbl_tank = make_value(scr, 140, 198, 200, 76, &micro_64);
    lbl_fl   = make_value(scr,  91, 120, 110, 52, &micro_41);
    lbl_fr   = make_value(scr, 279, 120, 110, 52, &micro_41);
    lbl_rl   = make_value(scr,  91, 308, 110, 52, &micro_41);
    lbl_rr   = make_value(scr, 279, 308, 110, 52, &micro_41);
}

/* call from your update timer */
void air_screen_update(int tank, int fl, int fr, int rl, int rr)
{
    lv_label_set_text_fmt(lbl_tank, "%d", tank);
    lv_label_set_text_fmt(lbl_fl,   "%d", fl);
    lv_label_set_text_fmt(lbl_fr,   "%d", fr);
    lv_label_set_text_fmt(lbl_rl,   "%d", rl);
    lv_label_set_text_fmt(lbl_rr,   "%d", rr);

    /* tank goes red when it's low (placeholder: set to your compressor cut-in) */
    lv_obj_set_style_text_color(lbl_tank, tank < 110 ? RED : WHITE, 0);
}
```
The 110 psi low-tank warning is a placeholder. Set it to just under your compressor's turn-on pressure.

## Converting the PNG
Use the same converter as for the other screens: RGB565, C array. 480x480 RGB565 is 460 KB.

## Tweaking
Layout constants are at the top of `make_air.py`. It reuses the drawing code in `make_side.py`, so keep both files in the same folder.
Run `python make_air.py` to regenerate the PNG, the preview and `air_layout.json`.
