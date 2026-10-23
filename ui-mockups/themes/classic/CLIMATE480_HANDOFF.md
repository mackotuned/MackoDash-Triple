# Climate control page (480x480): UI handoff

This page has the same controls and look as the 800x800 climate page, re-laid out for a 480 round touch screen:
- **TEMP:** blend door bar along the top edge. C is white and H is red; the hot half is underlined red. It has − / + buttons, a % value and a thermometer icon.
- **MODE:** five icon buttons: FACE, BI-LEVEL, FEET, FEET/DEF, DEFROST. The active one is solid red, and its name shows in red under the row.
- **FAN:** a 4-segment bar along the bottom edge, OFF → MAX, with − / + buttons, the speed value and a fan icon.

On the 800 page every mode button had its own label. At 480 those labels would be too small to read,
so this page shows **one name line** for the active mode instead.

## Assets
All are RGB PNG with no alpha; the corners outside the circle are black.

| File | Size | Use |
|---|---|---|
| `climate480/climate_480_bg.png` | 480x480 | Background, with no mode selected |
| `climate480/tiles/mode_<id>_on.png` / `_off.png` x5 each | 80x84 | Active and normal look for each mode button |
| `climate480/tiles/name_<id>.png` x5, `name_none.png` | 220x24 | Active mode name line, drawn at **x 130, y 276** |
| `climate480/tiles/pressed_<btn>.png` x4 | 60x60 | Pressed look for the − / + buttons |
| `climate480/previews/*.png` | 480x480 | Finished-looking examples with sample values |
| `climate480/climate480_layout.json` | | Every coordinate below |

The tiles are cut from fully rendered pages and match the background texture, differing by at most 1/255 at their edges.

## Touch map
| Control | Touch box (x0, y0, x1, y1) | Tile x,y | Action |
|---|---|---|---|
| TEMP − | 89, 63, 163, 137 | 96, 70 | blend −5 % (repeat while held) |
| TEMP + | 317, 63, 391, 137 | 324, 70 | blend +5 % |
| FACE | 44, 192, 116, 264 | 40, 186 | mode = face |
| BI-LEVEL | 124, 192, 196, 264 | 120, 186 | mode = bilevel |
| FEET | 204, 192, 276, 264 | 200, 186 | mode = feet |
| FEET/DEF | 284, 192, 356, 264 | 280, 186 | mode = feetdef |
| DEFROST | 364, 192, 436, 264 | 360, 186 | mode = defrost |
| FAN − | 89, 339, 163, 413 | 96, 346 | fan −1 (stops at 0) |
| FAN + | 317, 339, 391, 413 | 324, 346 | fan +1 (stops at 4) |

The − / + touch boxes are 7 px larger on each side than the drawn button, so they're easier to hit.
When the mode changes, draw the old mode's `_off` tile, the new mode's `_on` tile, and the new `name_<id>` tile.

## What LVGL draws
**Values:** white, centre-aligned, digits about **42 px** tall (Microgramma). Make each label box about 15% taller than listed.

| Label | Box (x, y, w, h) | Text |
|---|---|---|
| temp | 186, 66, 108, 46 | `%d` (the red "%" is printed on the background) |
| fan | 186, 350, 108, 46 | `%d` |

**Bars:** both are centred at (240, 240), with outer radius **230** and width **14**. The angles are LVGL degrees, and segment 0 is on the left.
The angles are the **same as the 800 page**, so you can share the tables and just change the radius and width.

TEMP, 20 segments. Light round(% / 5) of them. Segments 0–9 are white and 10–19 are red; the highest lit segment is always red.
```
{216,219.5},{221.5,225},{227,230.5},{232.5,236},{238,241.5},{243.5,247},{249,252.5},{254.5,258},{260,263.5},{265.5,269},
{271,274.5},{276.5,280},{282,285.5},{287.5,291},{293,296.5},{298.5,302},{304,307.5},{309.5,313},{315,318.5},{320.5,324}
```
FAN, 4 segments, left (OFF) to right. Light as many as the speed; the highest lit segment is red.
```
{118.5,144},{91,116.5},{63.5,89},{36,61.5}
```

## Flow
This page behaves the same as the 800 version and stays in sync with the knob remote and the 800 page:
always draw what the HVAC controller **reports**. Use whatever opens it, for example the knob's SCREEN → (screen) → CLIMATE. It has no exit button.

## Editable source
Run `python make_climate480.py`. It needs `make_climate.py` and `make_airctl.py` in the same folder, because they share the icons and drawing code.
The layout constants are at the top of the script. Running it regenerates the background, tiles, previews and JSON file.
