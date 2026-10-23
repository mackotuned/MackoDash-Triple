# Air ride control mode: UI handoff

Touching the air ride page on the **right screen (480x480)** opens control mode:
- **Right screen:** switches to the control page (corner selector, UP / DOWN, AIR OUT, EXIT). The pressure readings are hidden.
- **Centre screen (800x800):** switches to the pressure page (tank, all four corners, compressor button). It always lights the corner group being controlled.
- **EXIT** puts both screens back to what they showed before.

The style is the same as the gauges: textured face, Microgramma, white `#ECECE7`, red `#E81E1E`.

## Assets
All are RGB PNG with no alpha; the corners outside the circle are black.

| File | Size | Use |
|---|---|---|
| `airctl/control_480_bg.png` | 480x480 | Right screen background, with every button in its normal state |
| `airctl/tiles/sel_<group>.png` x7 | 232x152 | Selector tile (car with the controlled wheels in red, plus the name), drawn at **x 124, y 44** |
| `airctl/tiles/pressed_<button>.png` x6 | see the table below | Pressed look for each button. Draw it at the button's x,y while the button is held. |
| `airctl/pressure_800_bg.png` | 800x800 | Centre screen background |
| `airctl/tiles/car_<group>.png` x8 | 264x392 | Car tile with the controlled wheels in red and the name, drawn at **x 268, y 226** (`car_none` = nothing selected) |
| `airctl/tiles/comp_<state>.png` x3 | 208x160 | Compressor button `off` / `auto` / `run`, drawn at **x 296, y 626** |
| `airctl/previews/*.png` | | Finished-looking screens with sample numbers, for reference |
| `airctl/airctl_layout.json` | | Every coordinate below, read straight from the generator |

`<group>` is one of `all`, `front`, `rear`, `fl`, `fr`, `rl`, `rr`.

**The tiles are cut from fully rendered pages.** The texture under them matches the background exactly; this was checked with 0 pixel difference and no seams.
So "highlighting" is just placing a different image at the same spot: no blending, no alpha.
Keep each tile on screen until you replace it, and redraw the background part only when the page opens.

## Right screen, 480x480: control page
| Button | Touch / tile box (x0, y0, x1, y1) | Pressed tile | Action |
|---|---|---|---|
| ‹ previous | 44, 82, 112, 150 | `pressed_prev.png` (68x68) | previous group |
| › next | 368, 82, 436, 150 | `pressed_next.png` (68x68) | next group |
| Selector (car area) | 124, 44, 356, 196 | none | **swipe left/right** = previous/next group |
| UP | 125, 203, 355, 275 | `pressed_up.png` (230x72) | raise the selected group while held |
| DOWN | 125, 279, 355, 351 | `pressed_down.png` (230x72) | lower the selected group while held |
| AIR OUT | 105, 359, 239, 419 | `pressed_airout.png` (134x60) | **hold 2 s**, then dump the selected group (see below) |
| EXIT | 241, 359, 375, 419 | `pressed_exit.png` (134x60) | leave control mode on both screens |

The pressed tile's x,y is the first two numbers of its box.

**Group order** (wraps around): `all → front → rear → fl → fr → rl → rr → all`. On entry, the group is `all`, or the one used last time.

**How each button behaves (suggested):**
- **UP / DOWN:** momentary. Valve open while pressed, closed on release. Add a safety timeout (for example 8 s) in case a release event is lost.
- **AIR OUT:** a deliberate action. Show the pressed tile while holding, and fire only after 2 s of continuous hold.
  After that, either keep dumping until release, or dump to a set minimum. That's your call; the HOLD label covers both.
- **Every group change:** tell the centre screen so its car tile follows.

## Centre screen, 800x800: pressure page
**Numbers (LVGL labels):** white, centre-aligned. Digits about **72 px** tall (Microgramma digits), and the tank value uses the same size.

| Label | Box (x, y, w, h) |
|---|---|
| tank | 290, 120, 220, 74 |
| fl | 88, 300, 190, 72 |
| fr | 522, 300, 190, 72 |
| rl | 88, 488, 190, 72 |
| rr | 522, 488, 190, 72 |

Turn the controlled corners' numbers **red** (`#E81E1E`); the rest stay white. Make the label boxes about 15% taller than listed so the font's line height fits.

**Tank bar:** 13 segments at the top of the screen. Outer radius **383**, width **21**, centre (400, 400), range 0–200 psi.
The unlit segments are in the background. Light segments 0…n-1 with n = round(tank / 200 × 13). The highest lit segment is red, the rest white.
The angles are LVGL degrees (0 = 3 o'clock, clockwise), left to right:
```
{218,224.15},{226.15,232.31},{234.31,240.46},{242.46,248.62},{250.62,256.77},{258.77,264.92},{266.92,273.08},
{275.08,281.23},{283.23,289.38},{291.38,297.54},{299.54,305.69},{307.69,313.85},{315.85,322}
```

**Car tile:** at x 268, y 226. Swap to `car_<group>.png` whenever the right screen changes group, and to `car_none.png` if nothing is selected.

**Compressor button:** the tile is at x 296, y 626. The touch box is **340, 628, 460, 748**.
Tap cycles `off → auto → run → off`. Show the tile for the compressor's **actual** state, not just the last tap:
- `run`: red disc, "RUNNING"
- `auto`: white ring, "AUTO"
- `off`: dim, "COMP OFF"

If the centre screen isn't a touch screen, drive this from the knob remote or the right screen instead; the tile still shows the state.

## Screen flow
```
RIGHT: air ride page ──tap──► control page            CENTER: (normal gauge) ──► pressure page
          ▲                      │ ‹ › / swipe ─────────────────────────────────► car tile follows group
          │                      │ UP / DOWN (hold) ─────────────────────────────► numbers update live
          │                      │ AIR OUT (hold 2 s)
          └────── EXIT ──────────┘ ───────────────────────────────────────────── ► CENTER back to its previous page
```
The knob remote's AIR RIDE menu can open this same mode. Its corner picker and adjust pages use the same group names (`all`, `front`, `rear`, `fl`, `fr`, `rl`, `rr`).

## Editable source
Run `python make_airctl.py` (Python 3 + Pillow). The layout boxes are in the `R480` and `C800` tables, the car drawing is `car_top()`,
and the icons are `compressor_icon()`, `arrow_big()` and `chevron()`. Running it regenerates every background, tile, preview and the JSON file.
