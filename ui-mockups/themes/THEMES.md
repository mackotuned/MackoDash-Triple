# DigiDash themes: CLASSIC and GRAPHITE

```
themes/
  classic/    the EK Civic SiR look: textured face, Microgramma, needle gauges, red accents
  graphite/   modern and clean: flat graphite face, DIN lettering, arc gauges (no needles), ice-blue accent
```

**Both folders use the same file names and the same sub-folders.** To switch themes, change the asset root:
`themes/classic/…` ↔ `themes/graphite/…`. The only code that differs between themes is in the **gauge screens**, which change from needles to arcs (section 3).

## 1. What's identical across themes
These are pixel-for-pixel the same layout in both themes, so the same code works with either folder:

| Screen | Files | Same in both themes |
|---|---|---|
| Knob remote (240) | `knob_ui/*.png`, `knob_layout.json` | All 50 page names, menu order, value box, ring segment angles |
| Air control, right (480) | `airctl/control_480_bg.png`, `airctl/tiles/sel_*`, `pressed_*` | Every touch box and tile x,y and size |
| Air pressure, centre (800) | `airctl/pressure_800_bg.png`, `tiles/car_*`, `comp_*` | Tile positions, number boxes, tank segment angles |
| Climate (800) | `climate/…` | Touch map, tiles, number boxes, bar angles |
| Climate (480) | `climate480/…` | Touch map, tiles, name strip, number boxes, bar angles |

The handoff docs in `classic/` (`KNOB_UI_HANDOFF.md`, `AIR_CONTROL_HANDOFF.md`, `CLIMATE_HANDOFF.md`, `CLIMATE480_HANDOFF.md`)
apply to Graphite unchanged, **except for the colours**. Use the theme colours from section 2.

**Segmented rings in Graphite:** they're drawn as one continuous track with rounded ends. You can still light them segment by segment
with the same angle tables; the steps just touch, with no gaps. Or use one `lv_arc` with rounded ends instead (see section 3).

## 2. Theme colours and fonts
| Role | Classic | Graphite |
|---|---|---|
| Primary text / lit | `#ECECE7` | `#EEF0F2` |
| Accent (active, selected, normal fill) | `#E81E1E` (red) | `#5EC8F2` (ice blue) |
| Warning (redline, hot, AIR OUT, recording) | `#E81E1E` | `#FF564A` |
| Secondary text | `#969692` | `#868D97` |
| Unlit track / segment | `#2C2C2F` | `#262A30` |
| Text on a filled button | `#1A1A1C` | `#0C0E11` |
| Font | Microgramma D Extended Bold | Bahnschrift (labels: SemiBold; numbers: Light, weight about 330) |

How the **colours map when lighting things in LVGL**:
- **Classic:** lit = white, highest lit = red, warning = red.
- **Graphite:** lit = accent (ice blue), warning zone = warning red. There's no special colour for the highest lit segment.
  On the TEMP bars, the cold half is accent and the hot half is warning red.

Suggested theme struct:
```c
typedef struct {
    const char *root;            /* "/themes/classic" or "/themes/graphite" */
    lv_color_t text, accent, warn, text2, track, on_fill;
    const lv_font_t *num_big, *num_mid, *num_small, *label;
    bool gauges_use_needles;     /* classic: true, graphite: false */
    bool accent_tip_red;         /* classic: highest lit segment red */
} dd_theme_t;

static const dd_theme_t THEME_CLASSIC  = { "/themes/classic",  LV_COLOR_MAKE(0xEC,0xEC,0xE7), LV_COLOR_MAKE(0xE8,0x1E,0x1E),
    LV_COLOR_MAKE(0xE8,0x1E,0x1E), LV_COLOR_MAKE(0x96,0x96,0x92), LV_COLOR_MAKE(0x2C,0x2C,0x2F), LV_COLOR_MAKE(0x1A,0x1A,0x1C),
    &micro_72, &micro_41, &micro_26, &micro_14, true, true };
static const dd_theme_t THEME_GRAPHITE = { "/themes/graphite", LV_COLOR_MAKE(0xEE,0xF0,0xF2), LV_COLOR_MAKE(0x5E,0xC8,0xF2),
    LV_COLOR_MAKE(0xFF,0x56,0x4A), LV_COLOR_MAKE(0x86,0x8D,0x97), LV_COLOR_MAKE(0x26,0x2A,0x30), LV_COLOR_MAKE(0x0C,0x0E,0x11),
    &bahn_light_72, &bahn_light_41, &bahn_light_26, &bahn_semibold_14, false, false };
```
To switch: set the active theme, then rebuild the current screen. Each screen's create function reloads its background and tiles from `theme->root`.

## 3. Graphite gauge screens (arcs, no needles)
In Graphite, the four gauge backgrounds have the tracks, scale and labels printed on them. Use **`lv_arc`** in place of the needle:
show only the indicator part, with rounded ends, and leave the track to the PNG.
`gauges_layout.json` has every number below.

```c
static lv_obj_t *dd_arc(lv_obj_t *p, int size, int width, int bg_start, int bg_end,
                        int min, int max, bool reverse, lv_color_t col)
{
    lv_obj_t *a = lv_arc_create(p);
    lv_obj_set_size(a, size, size);
    lv_obj_center(a);
    lv_arc_set_bg_angles(a, bg_start, bg_end);
    lv_arc_set_range(a, min, max);
    if (reverse) lv_arc_set_mode(a, LV_ARC_MODE_REVERSE);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);        /* track is in the PNG */
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, col, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    return a;
}
```

| Screen | Arc: obj size, width, bg_start → bg_end, mode, range | Numbers (x, y, w, h) and digit size |
|---|---|---|
| **Tach 800** `tach_ek_800.png` | 702, 26, 135 → 45, normal, 0–9000. Switch the indicator to warning red at ≥ 8000. | rpm 230, 326, 340, 128 at **104 px** |
| **Speedo 480** `speedo_480_round.png` | 424, 16, 135 → 45, normal, 0–160 | mph 140, 186, 200, 92 at **76 px** |
| **Side 480** `side_480_round.png`, fuel (left) | 448, 12, 130 → 230, normal, 0–100 % | boost 140, 164, 200, 76 at **54 px** |
| Side, water (right) | 448, 12, 310 → 50, **reverse**, 0–100. Map: 60 °F → 0, **190 °F → 50** (middle tick), 225 °F → 100. Red above 80. | air temp 122, 296, 108, 40 and battery 250, 296, 108, 40, both at **28 px** |
| **Air 480** `air_480_round.png`, FL | 444, 12, 192 → 256, normal, 0–150 psi | tank 160, 210, 160, 58 at **44 px** |
| Air, FR | 444, 12, 284 → 348, **reverse**, 0–150 | FL 92, 130, 112, 46 and FR 276, 130, 112, 46 |
| Air, RR | 444, 12, 12 → 76, normal, 0–150 | RL 92, 298, 112, 46 and RR 276, 298, 112, 46 |
| Air, RL | 444, 12, 104 → 168, **reverse**, 0–150 | corner digits at **30 px** |

The corner arcs fill from each side towards the front or the rear, so the four bars mirror each other.
The water arc fills upwards from the bottom-right, which is why it uses reverse mode.

Water mapping in code:
```c
static int water_to_arc(float t) {                 /* 60F->0, 190F->50, 225F->100 */
    float v = t <= 60 ? 0 : t <= 190 ? 50.0f * (t - 60) / 130.0f : 50.0f + 50.0f * (t - 190) / 35.0f;
    return v > 100 ? 100 : (int)(v + 0.5f);
}
```

**Classic gauge screens** work as before: needles on the tach and speedo, and segments on the side and air screens.
Their notes (`LVGL_NOTES.md`, `SPEEDO_NOTES.md`, `SIDE_SCREEN_NOTES.md`, `AIR_SCREEN_NOTES.md`) are in `classic/`.

## 4. Fonts for Graphite
Convert **Bahnschrift** (`C:\Windows\Fonts\bahnschrift.ttf`, a variable font) with LVGL's font converter, at **Light** for the
numbers (72, 54, 44, 41, 30, 28, 26 px as needed) and **SemiBold** for any labels you draw.
Bahnschrift ships with Windows; check its licence before you distribute the firmware. If that's a problem, **Barlow**
(SIL Open Font License) looks very close and can be swapped in by changing `FONT` in `graphite/src/theme.py`.

## 5. Editable source
- `graphite/src/theme.py`: the palette, font, face and drawing canvas. Change the palette here to re-colour everything.
- `graphite/src/make_gauges.py`: tach, speedo, side and air screens.
- `graphite/src/make_knob.py`, `make_airctl.py`, `make_climate.py`, `make_climate480.py`: the classic generators with the
  Graphite theme applied. Every coordinate is unchanged.

To rebuild, run these from inside `themes/graphite/`:
```bash
python src/make_gauges.py && python src/make_knob.py && python src/make_airctl.py && python src/make_climate.py && python src/make_climate480.py
```
`classic/src/` has the original generators. Run those from inside `themes/classic/`.
