# Climate control page (800x800): UI handoff

This page has the same three controls as the knob remote's CLIMATE menu, all on one touch screen:

| Area | Control | Range |
|---|---|---|
| Top edge | **TEMP**: blend door bar, C (white) → H (red), with − / + buttons and a % value | 0–100 %, 5 % per tap, 20 segments |
| Middle | **MODE**: FACE, BI-LEVEL, FEET, FEET/DEF, DEFROST. The active mode is red. | one of 5 |
| Bottom edge | **FAN**: speed bar, OFF → MAX, with − / + buttons and a speed value | 0–4, 4 segments |

The style matches the gauges: textured face, Microgramma, white `#ECECE7`, red `#E81E1E`.
The mode icons are the same drawings as on the knob remote, so both controls look the same.

## Assets
All are RGB PNG with no alpha; the corners outside the circle are black.

| File | Size | Use |
|---|---|---|
| `climate/climate_800_bg.png` | 800x800 | Background: all labels, icons, unlit bars and buttons, with no mode selected |
| `climate/tiles/mode_<id>_on.png` x5 | 116x146 | The active-mode look, including its red label. Draw it at `tile_xy`. |
| `climate/tiles/mode_<id>_off.png` x5 | 116x146 | The normal look; use it to clear the previously active mode |
| `climate/tiles/pressed_<btn>.png` x4 | 92x92 | Pressed look for the − / + buttons |
| `climate/previews/*.png` | 800x800 | Finished-looking examples with sample values |
| `climate/climate_layout.json` | | Every coordinate below |

The tiles are cut from fully rendered pages, so they line up with the background texture. The − / + tiles match exactly.
The mode tiles differ by at most 3/255 at their edges, which isn't visible.

## Touch map
| Control | Touch box (x0, y0, x1, y1) | Tile x,y | Action |
|---|---|---|---|
| TEMP − | 166, 130, 258, 222 | 166, 130 | blend −5 % (repeat while held, about every 150 ms) |
| TEMP + | 542, 130, 634, 222 | 542, 130 | blend +5 % |
| FACE | 106, 346, 214, 484 | 102, 342 | mode = face |
| BI-LEVEL | 226, 346, 334, 484 | 222, 342 | mode = bilevel |
| FEET | 346, 346, 454, 484 | 342, 342 | mode = feet |
| FEET/DEF | 466, 346, 574, 484 | 462, 342 | mode = feetdef |
| DEFROST | 586, 346, 694, 484 | 582, 342 | mode = defrost |
| FAN − | 166, 578, 258, 670 | 166, 578 | fan −1 (stops at 0, OFF) |
| FAN + | 542, 578, 634, 670 | 542, 578 | fan +1 (stops at 4) |

The mode touch boxes include the label under each button.
When a mode changes, draw `mode_<old>_off` and then `mode_<new>_on`. Always show the mode the HVAC controller **reports**, so the knob and the screen stay in sync.

## What LVGL draws
**Values:** white, centre-aligned. Digits about **72 px** tall (Microgramma). Make each label box about 15% taller than listed.

| Label | Box (x, y, w, h) | Text |
|---|---|---|
| temp | 330, 118, 140, 74 | `%d` (the red "%" is printed on the background) |
| fan | 330, 608, 140, 74 | `%d` (0 = off; show `0`, or `OFF` if your font has letters) |

**Bars:** both are centred at (400, 400), with outer radius **383** and width **21**. The angles are LVGL degrees (0 = 3 o'clock, clockwise), and segment 0 is on the left.

TEMP, 20 segments. Light round(% / 5) of them. Segments 0–9 are white and segments 10–19 are red (the hot half); the highest lit segment is always red.
```
{216,219.5},{221.5,225},{227,230.5},{232.5,236},{238,241.5},{243.5,247},{249,252.5},{254.5,258},{260,263.5},{265.5,269},
{271,274.5},{276.5,280},{282,285.5},{287.5,291},{293,296.5},{298.5,302},{304,307.5},{309.5,313},{315,318.5},{320.5,324}
```
FAN, 4 segments, left (OFF side) to right. Light as many as the speed; the highest lit segment is red.
```
{118.5,144},{91,116.5},{63.5,89},{36,61.5}
```

## Flow
- **Opening the page:** the knob's SCREEN → (pick a screen) → … can switch the centre screen to climate, or use whatever trigger you like.
- **On open:** draw the background, then the current mode's `_on` tile, then the values and bars from the HVAC controller's last report.
- **Two-way sync:** a change from the knob remote (fan, temp, mode) updates this page live, and a tap here updates the knob's adjust pages the same way.
- **No exit button:** this page has none because it's a display page. If you want one, the clear space under the fan value is a good spot.

## Editable source
Run `python make_climate.py` (Python 3 + Pillow). It imports the drawing code from `make_airctl.py`, so keep both files in the same folder.
Positions, the arcs and the mode list are constants at the top of the script, and the icons are `ic_mode`, `ic_defrost`, `ic_thermo` and `ic_fan`.
Running it regenerates the background, tiles, previews and JSON file.
