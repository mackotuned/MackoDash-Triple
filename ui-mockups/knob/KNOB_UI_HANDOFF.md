# DigiDash knob remote: UI handoff

This is a remote control for the three DigiDash gauge screens. It does no processing of its own:
every action becomes a message to the screens over your network. The visual style matches the gauges:
the same textured black face, Microgramma lettering, white ink `#ECECE7` and red `#E81E1E`.

## 1. Hardware
| | |
|---|---|
| Listing | Amazon B0HFS1QWSB, "1.28 Inch ESP32 Rotary Display 240x240 HMI IPS Round Knob Touch Display" |
| Board (very likely) | **Elecrow CrowPanel 1.28" HMI ESP32 Rotary Display**, SKU DHE38128D |
| MCU | ESP32-S3R8: dual-core LX7 at 240 MHz, **8 MB PSRAM, 16 MB flash** |
| Display | 1.28" round IPS, **240 x 240**, GC9A01 driver over SPI |
| Touch | Capacitive, CST816D |
| Input | Rotary knob (rotate) plus touch. Check that your unit's knob also has a push switch; if not, use the touch alternatives in section 3. |

The Amazon page doesn't name the brand. The title, size and features match the Elecrow board, so confirm against the sticker or silkscreen on your unit.
Elecrow's pin map and LVGL starter project are in their wiki ([Arduino LVGL rotary guide](https://www.elecrow.com/wiki/1.28_Arduino_LVGL_Rotary_Guide.html)).

## 2. Assets
All assets are **240 x 240 px PNG, RGB, no alpha**. The corners outside the circle are pure black.

| Folder or file | What it is | Count |
|---|---|---|
| `knob_ui/main_*.png` | Main menu pages | 6 |
| `knob_ui/screen_target_*.png` | SCREEN: choose which screen | 4 |
| `knob_ui/screen_source_*.png` | SCREEN: choose what that screen shows | 6 |
| `knob_ui/air_*.png` | AIR RIDE: choose corner or group | 8 |
| `knob_ui/adjust_air_*.png` | AIR RIDE: pressure adjust background, one per corner or group | 7 |
| `knob_ui/climate_*.png` | CLIMATE submenu | 4 |
| `knob_ui/adjust_fan.png`, `adjust_temp.png` | Fan speed and blend door adjust backgrounds | 2 |
| `knob_ui/mode_*.png` | Mode door options | 6 |
| `knob_ui/logs_*.png` | LOGS: choose which screen opens the log viewer | 4 |
| `knob_ui/rec_idle.png`, `rec_active.png` | Data log record: start / stop states | 2 |
| `knob_ui/adjust_bright.png` | Brightness adjust background | 1 |
| `knob_ui/knob_layout.json` | Menu tree, value boxes and ring segment angles, read straight from the generator | 1 |
| `knob_ui_previews/*.png` | Adjust and record pages with sample values filled in, so you can see what LVGL should produce | 6 |
| `knob_ui_contact_sheet.png` | Every page on one sheet | 1 |

The menu pages are **finished images**: the icon, name, header, side arrows and position marker are all baked in.
The 7 adjust and record backgrounds need LVGL to draw the value and the lit ring segments (section 5).

**Storage options:**
- **C arrays, RGB565:** 115,200 bytes per image, about 5.8 MB for all 50. That fits in the 16 MB flash.
- **PNG on LittleFS:** about 1.6 MB in total, decoded with LVGL's PNG decoder into PSRAM. This uses less flash but loads more slowly.

If you see banding in the dark texture with RGB565, turn on dithering in the converter.

## 3. Interaction map
| Input | On a menu page | On an adjust page | On the record page |
|---|---|---|---|
| Knob clockwise, one detent | Next item (wraps around) | Value + 1 step | nothing |
| Knob anticlockwise | Previous item (wraps around) | Value − 1 step | nothing |
| **Press** (or **tap** on touch) | Open or select the item | Confirm, then go back to the parent menu | Start / stop recording |
| **Long press** (≥ 600 ms), or **swipe down** | Back to the parent menu (does nothing on main) | Cancel: restore the old value, then go back | Back to main (recording keeps running) |
| Swipe left / right (touch) | Same as anticlockwise / clockwise | Same as anticlockwise / clockwise | nothing |
| Idle for 20 s | Return to main menu, item 0 | Same as long press (cancel) | Return to main menu |

Every menu that isn't the main menu ends with a **BACK** item, which does the same thing as a long press.
Adjust pages send their value **live while you turn**, limited to about 10 messages per second. Press only confirms and leaves the page.

## 4. Screen flow and expected actions
The image names match the files in `knob_ui/`. The action names are only suggestions; use whatever your protocol calls them.

```
MAIN  (main_0..5)
├─ SCREEN ─────────► screen_target_*   LEFT | CENTER | RIGHT | BACK
│                      └─ (pick one) ─► screen_source_*   TACH | SPEEDO | DATA | AIR RIDE | LOGS | BACK
│                                         press → screen.set {target, source} → back to MAIN
├─ REC ────────────► rec_idle  ──press──► rec_active (timer runs)   log.start / log.stop
│                    rec_active ──press─► rec_idle
├─ AIR RIDE ───────► air_*   ALL | FRONT | REAR | FL | FR | RL | RR | BACK
│                      └─ (pick one) ─► adjust_air_<group>
│                                         turn → air.set {group, psi}   (live)
│                                         press → back to air_*   long → air.set old value, back
├─ CLIMATE ────────► climate_*   FAN | TEMP | MODE | BACK
│                      FAN  ─► adjust_fan    turn → hvac.fan {0..4}
│                      TEMP ─► adjust_temp   turn → hvac.blend {0..100 %}
│                      MODE ─► mode_*   FACE | BI-LEVEL | FEET | FEET/DEF | DEFROST | BACK
│                                press → hvac.mode {id} → back to climate_*
├─ LOGS ───────────► logs_*   LEFT | CENTER | RIGHT | BACK
│                      press → logs.open {target} → back to MAIN
└─ BRIGHTNESS ─────► adjust_bright   turn → display.brightness {5..100 %}  (all 3 screens + the knob itself)
```

**Opening an adjust page:** show the **current** value from the screens. If you don't have it yet, show the last
value you sent and leave the segments unlit until a reply arrives. Placing the cursor in a submenu on the
item that's currently active (for example, the current mode) is nice but optional.

## 5. Adjust pages: what LVGL draws
All 7 adjust backgrounds share one layout.

**Value label:** box **x 40, y 96, w 160, h 48**, centre-aligned, white. Digits about 36 px tall (Microgramma digits).

**Ring:** outer radius **113 px**, width **11 px**, so the arc object is 226 x 226 centred at (120, 120). Segment 0 is at the lower left and they run clockwise.
The unlit segments are in the PNG. Light segments 0…n-1 by unhiding arc objects (the same method as the side screen).
The **highest lit segment is red** and the rest are white. On TEMP, every lit segment from **10 upward is red** (the hot half).

20-segment ring (air, temp, brightness), LVGL degrees (0 = 3 o'clock, clockwise):
```c
static const seg_t RING20[20] = {
 {140,151.1},{153.1,164.2},{166.2,177.3},{179.3,190.4},{192.4,203.5},
 {205.5,216.6},{218.6,229.7},{231.7,242.8},{244.8,255.9},{257.9,269},
 {271,282.1},{284.1,295.2},{297.2,308.3},{310.3,321.4},{323.4,334.5},
 {336.5,347.6},{349.6,0.7},{2.7,13.8},{15.8,26.9},{28.9,40}};
```
4-segment ring (fan): `{140,203.5},{205.5,269},{271,334.5},{336.5,40}`

One segment `{349.6,0.7}` crosses 0°. LVGL arcs handle that, but if your own code compares angles, add 360 to the end value.

| Page | Range | Step per detent | Segments lit | Value text |
|---|---|---|---|---|
| adjust_air_* | 0–150 psi | 1 psi | round(psi / 150 × 20) | `%d` |
| adjust_fan | 0–4 | 1 | = speed | `%d` (0 shows `OFF`, if your font has letters) |
| adjust_temp | 0–100 % blend | 5 % | round(% / 5) | `%d` |
| adjust_bright | 5–100 % | 5 % | round(% / 5) | `%d` |

**Record timer** (rec_active only): box **x 60, y 176, w 120, h 18**, centre-aligned, **red**, digits about 14 px tall, formatted `mm:ss`.
See `knob_ui_previews/preview_rec_active.png`.

## 6. Fonts and colours
- Font: *Microgramma D Extended Bold*, converted with LVGL's font converter. Make two sizes, about 36 px and 14 px, with the characters `0123456789:%OFN`.
  The menus need no fonts because all their text is in the images.
- White `#ECECE7`, red `#E81E1E`, unlit segment `#2C2C2F`, face base `#131315`.

## 7. Editable source
`make_knob.py` (Python 3 with Pillow) draws every page from code. **The icons are drawn with code, not imported art.**
- **Menus:** change the `MAIN` / `SUBMENUS` tables to add, remove, rename or reorder items. The position marker and file names follow automatically.
- **Icons:** each icon is a small function (`ic_fan`, `ic_car`, `ic_mode`, …) drawn in a 100x100 box.
- **Adjust pages:** built by the `adjust_page(...)` calls at the bottom of the script.
- **Colours:** set at the top of the file.

Run `python make_knob.py` to regenerate `knob_ui/`, `knob_ui_previews/`, `knob_layout.json` and the contact sheet.
It needs the Microgramma font installed; without it, it falls back to Bahnschrift.
