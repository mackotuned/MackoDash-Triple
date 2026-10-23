"""
Climate control page - 480x480 ROUND (touch). Same controls and look as the 800 climate page,
re-laid out for the smaller screen (not just scaled): bigger touch targets relative to the screen,
mode labels replaced by one name line under the row so nothing is tiny.

  top     : TEMP bar (20 segs, C -> H), - / +, % value, thermometer
  middle  : MODE row (5 icon buttons) + active mode name underneath
  bottom  : FAN bar (4 segs, OFF -> MAX), - / +, speed value, fan icon

Needs make_climate.py and make_airctl.py in the same folder (shared icons and drawing code).
Run:  python make_climate480.py  ->  climate480/
"""
import json
import math
import os
from PIL import Image, ImageFilter
from make_airctl import Canvas, INK_WHITE, INK_RED, NUM_SQUEEZE
from make_climate import segs_of, draw_mode_icon, ic_thermo, ic_fan, MODES
from theme import NUMBER_WEIGHT, WARN as INK_WARN

OUT_DIR = "climate480"
D = 480
CX = CY = 240

TEMP_ARC = dict(r_out=230, r_in=216, a0=-54, a1=54, n=20)
FAN_ARC = dict(r_out=230, r_in=216, a0=234, a1=126, n=4)

TEMP_VALUE = (186, 66, 108, 46)
FAN_VALUE = (186, 350, 108, 46)
BTN_R = 27
TEMP_MINUS, TEMP_PLUS = (126, 100), (354, 100)
FAN_MINUS, FAN_PLUS = (126, 376), (354, 376)

MODE_BTN, MODE_GAP, MODE_Y = 72, 8, 192
NAME_STRIP = (130, 276, 350, 300)          # x0, y0, x1, y1 of the active-mode name tile


def mode_boxes():
    total = len(MODES) * MODE_BTN + (len(MODES) - 1) * MODE_GAP
    x0 = CX - total / 2
    return [(round(x0 + i * (MODE_BTN + MODE_GAP)), MODE_Y, round(x0 + i * (MODE_BTN + MODE_GAP) + MODE_BTN),
             MODE_Y + MODE_BTN) for i in range(len(MODES))]


def plusminus(cv, cx, cy, plus, pressed):
    if pressed:
        cv.disc("w", cx, cy, BTN_R)
        ink = "k"
    else:
        cv.ring("w", cx, cy, BTN_R, 2.2)
        ink = "w"
    cv.line(ink, [(cx - 10, cy), (cx + 10, cy)], 3.8)
    if plus:
        cv.line(ink, [(cx, cy - 10), (cx, cy + 10)], 3.8)


def page(mode=None, pressed=None):
    cv = Canvas(D, SS=4)
    rm = (TEMP_ARC["r_out"] + TEMP_ARC["r_in"]) / 2
    pt = lambda r, a: (CX + r * math.sin(math.radians(a)), CY - r * math.cos(math.radians(a)))

    # temp
    cv.arc_track("seg", CX, CY, (TEMP_ARC["r_out"] + TEMP_ARC["r_in"]) / 2, TEMP_ARC["r_out"] - TEMP_ARC["r_in"], min(TEMP_ARC["a0"], TEMP_ARC["a1"]), max(TEMP_ARC["a0"], TEMP_ARC["a1"]))
    for i, (a, b) in enumerate(segs_of(TEMP_ARC)):
        pass
        if i >= TEMP_ARC["n"] // 2:
            cv.sector("x", CX, CY, TEMP_ARC["r_in"] - 3.5, TEMP_ARC["r_in"] - 5.5, a, b)
    for txt, a, ink in (("C", TEMP_ARC["a0"] - 7, "r"), ("H", TEMP_ARC["a1"] + 7, "x")):
        x, y = pt(rm, a)
        cv.text_center(ink, txt, 10, x, y + 4)
    ic_thermo(cv, CX, 140, 36)
    cv.text("r", "%", 8, CX + 60, 82, anchor="l")
    plusminus(cv, *TEMP_MINUS, False, pressed == "temp_minus")
    plusminus(cv, *TEMP_PLUS, True, pressed == "temp_plus")

    # mode
    cv.text("dim", "MODE", 7.5, CX, MODE_Y - 18)
    for key, (x0, y0, x1, y1) in zip([k for k, _ in MODES], mode_boxes()):
        active = key == mode
        if active:
            cv.rrect("r", x0, y0, x1, y1, 11, fill=True)
        else:
            cv.rrect("w", x0, y0, x1, y1, 11, 2.0)
        draw_mode_icon(cv, key, (x0 + x1) / 2, (y0 + y1) / 2, 50, "k" if active else "w", "k" if active else "r")
    if mode:
        cv.text("r", dict(MODES)[mode], 9.5, CX, NAME_STRIP[1] + 6, max_w=NAME_STRIP[2] - NAME_STRIP[0] - 8)

    # fan
    cv.arc_track("seg", CX, CY, (FAN_ARC["r_out"] + FAN_ARC["r_in"]) / 2, FAN_ARC["r_out"] - FAN_ARC["r_in"], min(FAN_ARC["a0"], FAN_ARC["a1"]), max(FAN_ARC["a0"], FAN_ARC["a1"]))
    for a, b in segs_of(FAN_ARC):
        pass
    for txt, a in (("OFF", FAN_ARC["a0"] + 9), ("MAX", FAN_ARC["a1"] - 9)):
        x, y = pt(rm, a)
        cv.text_center("w", txt, 7.5, x, y - 2)
    ic_fan(cv, CX, 326, 34)
    plusminus(cv, *FAN_MINUS, False, pressed == "fan_minus")
    plusminus(cv, *FAN_PLUS, True, pressed == "fan_plus")
    return cv.render()


def to_lvgl(d12):
    return round((d12 + 270) % 360, 2)


def preview(img, temp_lit, fan_lit, temp_txt, fan_txt):
    cv = Canvas(D, SS=4)
    big = img.resize((cv.S, cv.S), Image.LANCZOS)
    for i, (a, b) in enumerate(segs_of(TEMP_ARC)[:temp_lit]):
        cv.sector("x" if i >= TEMP_ARC["n"] // 2 else "r", CX, CY,
                  TEMP_ARC["r_out"], TEMP_ARC["r_in"], a, b)
    fan_lr = sorted(segs_of(FAN_ARC), key=lambda ab: -ab[0])
    for i, (a, b) in enumerate(fan_lr[:fan_lit]):
        cv.sector("r", CX, CY, FAN_ARC["r_out"], FAN_ARC["r_in"], a, b)
    for txt, (x, y, w, h) in ((temp_txt, TEMP_VALUE), (fan_txt, FAN_VALUE)):
        g = cv.text_mask(txt, 30, NUM_SQUEEZE, weight=NUMBER_WEIGHT)
        cv.m["w"].paste(255, (int(cv.s(x + w / 2) - g.width / 2), int(cv.s(y + h / 2) - g.height / 2)), g)
    for k, col in (("w", INK_WHITE), ("r", INK_RED), ("x", INK_WARN)):
        big = Image.composite(Image.new("RGB", big.size, col), big, cv.m[k].filter(ImageFilter.GaussianBlur(1.2)))
    return big.resize((D, D), Image.LANCZOS)


def main():
    os.makedirs(os.path.join(OUT_DIR, "tiles"), exist_ok=True)
    os.makedirs(os.path.join(OUT_DIR, "previews"), exist_ok=True)
    bg = page()
    bg.save(os.path.join(OUT_DIR, "climate_480_bg.png"))

    modes = []
    ns = NAME_STRIP
    for (key, name), (x0, y0, x1, y1) in zip(MODES, mode_boxes()):
        img = page(mode=key)
        box = (x0 - 4, y0 - 6, x1 + 4, y1 + 6)
        img.crop(box).save(os.path.join(OUT_DIR, "tiles", "mode_%s_on.png" % key))
        bg.crop(box).save(os.path.join(OUT_DIR, "tiles", "mode_%s_off.png" % key))
        img.crop(ns).save(os.path.join(OUT_DIR, "tiles", "name_%s.png" % key))
        modes.append(dict(id=key, name=name, touch_box=[x0, y0, x1, y1], tile_xy=list(box[:2]),
                          tile_size=[box[2] - box[0], box[3] - box[1]],
                          tile_on="tiles/mode_%s_on.png" % key, tile_off="tiles/mode_%s_off.png" % key,
                          name_tile="tiles/name_%s.png" % key))
    bg.crop(ns).save(os.path.join(OUT_DIR, "tiles", "name_none.png"))
    btns = {}
    for key, (cx, cy) in (("temp_minus", TEMP_MINUS), ("temp_plus", TEMP_PLUS),
                          ("fan_minus", FAN_MINUS), ("fan_plus", FAN_PLUS)):
        box = (cx - BTN_R - 3, cy - BTN_R - 3, cx + BTN_R + 3, cy + BTN_R + 3)
        page(pressed=key).crop(box).save(os.path.join(OUT_DIR, "tiles", "pressed_%s.png" % key))
        btns[key] = dict(touch_box=[cx - BTN_R - 10, cy - BTN_R - 10, cx + BTN_R + 10, cy + BTN_R + 10],
                         tile_xy=list(box[:2]), tile_size=[box[2] - box[0]] * 2,
                         pressed_tile="tiles/pressed_%s.png" % key)
    fan_lr = sorted(segs_of(FAN_ARC), key=lambda ab: -ab[0])
    layout = dict(
        background="climate_480_bg.png", size=[D, D],
        name_strip=dict(tile_xy=list(ns[:2]), tile_size=[ns[2] - ns[0], ns[3] - ns[1]], none_tile="tiles/name_none.png"),
        temp=dict(value_box=dict(zip("xywh", TEMP_VALUE)), range=[0, 100], step=5,
                  ring=dict(center=[CX, CY], radius_outer=TEMP_ARC["r_out"], width=TEMP_ARC["r_out"] - TEMP_ARC["r_in"],
                            segments=[dict(start=to_lvgl(a), end=to_lvgl(b)) for a, b in segs_of(TEMP_ARC)],
                            red_from_segment=TEMP_ARC["n"] // 2)),
        fan=dict(value_box=dict(zip("xywh", FAN_VALUE)), range=[0, 4], step=1,
                 ring=dict(center=[CX, CY], radius_outer=FAN_ARC["r_out"], width=FAN_ARC["r_out"] - FAN_ARC["r_in"],
                           segments=[dict(start=to_lvgl(a), end=to_lvgl(b)) for a, b in fan_lr])),
        mode=modes, buttons=btns)
    with open(os.path.join(OUT_DIR, "climate480_layout.json"), "w") as f:
        json.dump(layout, f, indent=2)

    preview(page(mode="bilevel"), 12, 2, "60", "2").save(os.path.join(OUT_DIR, "previews", "climate480_bilevel_60_fan2.png"))
    preview(page(mode="feetdef", pressed="temp_plus"), 17, 3, "85", "3").save(
        os.path.join(OUT_DIR, "previews", "climate480_feetdef_85_fan3_pressed.png"))
    print("done")


if __name__ == "__main__":
    main()
