"""
Climate control page - 800x800 ROUND (touch), same theme as the gauges and the knob remote.

  top     : TEMP / blend door - 20-segment bar along the top edge, C (white) -> H (red), - / + buttons, % value
  middle  : MODE - FACE | BI-LEVEL | FEET | FEET/DEF | DEFROST buttons, active one in red
  bottom  : FAN - 4-segment bar along the bottom edge, OFF -> MAX, - / + buttons, speed value

Static art is in the background; the active mode button and pressed - / + buttons are drop-in TILES
cut from fully rendered pages (texture lines up exactly). LVGL lights bar segments and draws the two numbers.

Uses the Canvas class from make_airctl.py (keep both files in the same folder).
Run:  python make_climate.py  ->  climate/ (background, tiles, previews, climate_layout.json)
"""
import json
import math
import os
from PIL import Image, ImageDraw, ImageChops, ImageFilter
from make_airctl import Canvas, INK_WHITE, INK_RED, NUM_SQUEEZE
from theme import NUMBER_WEIGHT, WARN as INK_WARN

OUT_DIR = "climate"
D = 800

TEMP_ARC = dict(r_out=383, r_in=362, a0=-54, a1=54, n=20)        # from-12 degrees, left -> right
FAN_ARC = dict(r_out=383, r_in=362, a0=234, a1=126, n=4)         # bottom: left (234) -> right (126)
SEG_GAP = 0.0                      # GRAPHITE: continuous tracks

TEMP_VALUE = (330, 118, 140, 74)
FAN_VALUE = (330, 608, 140, 74)
BTN_R = 42
TEMP_MINUS, TEMP_PLUS = (212, 176), (588, 176)
FAN_MINUS, FAN_PLUS = (212, 624), (588, 624)

MODES = [("face", "FACE"), ("bilevel", "BI-LEVEL"), ("feet", "FEET"), ("feetdef", "FEET/DEF"), ("defrost", "DEFROST")]
MODE_BTN = 108                     # square size
MODE_GAP = 12
MODE_Y = 346                       # top of the mode buttons


# ------------------------------------------------------------------ icon pen (100-unit box, screen px)
class Pen:
    def __init__(self, cv, cx, cy, size):
        self.cv, self.cx, self.cy, self.u = cv, cx, cy, size / 100.0

    def P(self, x, y):
        return self.cx + x * self.u, self.cy + y * self.u

    def line(self, pts, w, ink):
        self.cv.line(ink, [self.P(*p) for p in pts], w * self.u)

    def dot(self, x, y, r, ink):
        self.cv.disc(ink, *self.P(x, y), r * self.u)

    def ring(self, x, y, r, w, ink):
        self.cv.ring(ink, *self.P(x, y), r * self.u, w * self.u)

    def poly(self, pts, ink):
        self.cv.poly(ink, [self.P(*p) for p in pts])

    def rrect(self, x0, y0, x1, y1, r, w, ink):
        self.cv.rrect(ink, *self.P(x0, y0), *self.P(x1, y1), r * self.u, w * self.u)

    def arrow(self, x0, y0, x1, y1, w, head, ink):
        a = math.atan2(y1 - y0, x1 - x0)
        bx, by = x1 - head * 0.8 * math.cos(a), y1 - head * 0.8 * math.sin(a)
        self.line([(x0, y0), (bx, by)], w, ink)
        px, py = -math.sin(a), math.cos(a)
        c, sn = math.cos(a), math.sin(a)
        self.poly([(x1, y1), (bx - head * 0.25 * c + px * head * 0.62, by - head * 0.25 * sn + py * head * 0.62),
                   (bx - head * 0.25 * c - px * head * 0.62, by - head * 0.25 * sn - py * head * 0.62)], ink)

    def wave_up(self, x, y0, y1, amp, w, ink):
        n = 24
        pts = [(x + amp * math.sin(i / n * math.pi * 2.2), y0 + (y1 - y0) * i / n) for i in range(n + 1)]
        self.line(pts[:-3], w, ink)
        self.arrow(pts[-4][0], pts[-4][1], pts[-1][0], pts[-1][1] - 2, w, 10, ink)


def ic_mode(p, ink, accent, face=False, feet=False, defrost=False):
    p.dot(-14, -26, 8, ink)
    p.line([(-18, -14), (-20, 10), (8, 10), (10, 34)], 7, ink)
    if face:
        p.arrow(40, -26, 6, -26, 5.5, 12, accent)
    if feet:
        p.arrow(40, 34, 22, 34, 5.5, 12, accent)
    if defrost:
        p.line([(10, -48), (40, -26)], 6, ink)
        p.wave_up(27, 2, -28, 3, 4.5, accent)


def ic_defrost(p, ink, accent):
    p.line([(-38, 4), (38, 4), (29, -30), (-29, -30), (-38, 4)], 6, ink)
    for x in (-16, 0, 16):
        p.wave_up(x, 34, 4, 3.2, 4.5, accent)


def draw_mode_icon(cv, key, cx, cy, size, ink, accent):
    p = Pen(cv, cx, cy, size)
    if key == "defrost":
        ic_defrost(p, ink, accent)
    else:
        ic_mode(p, ink, accent, face=key in ("face", "bilevel"), feet=key in ("bilevel", "feet", "feetdef"),
                defrost=key == "feetdef")


def ic_thermo(cv, cx, cy, size):
    p = Pen(cv, cx, cy, size)
    p.rrect(-8, -40, 8, 16, 8, 5.5, "w")
    p.ring(0, 26, 14, 5.5, "w")
    p.dot(0, 26, 8, "r")
    p.line([(0, 20), (0, -12)], 6, "r")
    for i in range(3):
        p.line([(14, -30 + i * 11), (22, -30 + i * 11)], 4, "w")


def ic_fan(cv, cx, cy, size, ink="w"):
    u = size / 100
    S, s = cv.S, cv.s
    m = Image.new("L", (S, S), 0)
    for k in range(4):
        b = Image.new("L", (S, S), 0)
        bx, by = cx + 9 * u, cy - 23 * u
        ImageDraw.Draw(b).ellipse([s(bx - 11 * u), s(by - 21 * u), s(bx + 11 * u), s(by + 21 * u)], fill=255)
        m = ImageChops.lighter(m, b.rotate(-(k * 90 + 18), resample=Image.BICUBIC, center=(s(cx), s(cy))))
    hub = Image.new("L", (S, S), 0)
    ImageDraw.Draw(hub).ellipse([s(cx - 9 * u), s(cy - 9 * u), s(cx + 9 * u), s(cy + 9 * u)], fill=255)
    m = ImageChops.subtract(m, hub)
    ImageDraw.Draw(m).ellipse([s(cx - 5 * u), s(cy - 5 * u), s(cx + 5 * u), s(cy + 5 * u)], fill=255)
    cv.m[ink].paste(255, (0, 0), m)


# ------------------------------------------------------------------ page
def segs_of(arc):
    a0, a1, n = arc["a0"], arc["a1"], arc["n"]
    sgn = 1 if a1 > a0 else -1
    seg = (abs(a1 - a0) - SEG_GAP * (n - 1)) / n
    out = []
    for i in range(n):
        a = a0 + sgn * i * (seg + SEG_GAP)
        b = a + sgn * seg
        out.append((min(a, b), max(a, b)))
    return out


def mode_boxes():
    total = len(MODES) * MODE_BTN + (len(MODES) - 1) * MODE_GAP
    x0 = 400 - total / 2
    return [(round(x0 + i * (MODE_BTN + MODE_GAP)), MODE_Y, round(x0 + i * (MODE_BTN + MODE_GAP) + MODE_BTN),
             MODE_Y + MODE_BTN) for i in range(len(MODES))]


def plusminus(cv, cx, cy, plus, pressed):
    if pressed:
        cv.disc("w", cx, cy, BTN_R)
        ink = "k"
    else:
        cv.ring("w", cx, cy, BTN_R, 2.2)
        ink = "w"
    cv.line(ink, [(cx - 15, cy), (cx + 15, cy)], 5.5)
    if plus:
        cv.line(ink, [(cx, cy - 15), (cx, cy + 15)], 5.5)


def climate_page(mode=None, pressed=None):
    cv = Canvas(D, SS=3)
    # temp bar + ends
    cv.arc_track("seg", 400, 400, (TEMP_ARC["r_out"] + TEMP_ARC["r_in"]) / 2, TEMP_ARC["r_out"] - TEMP_ARC["r_in"], min(TEMP_ARC["a0"], TEMP_ARC["a1"]), max(TEMP_ARC["a0"], TEMP_ARC["a1"]))
    for i, (a, b) in enumerate(segs_of(TEMP_ARC)):
        pass
    for i, (a, b) in enumerate(segs_of(TEMP_ARC)):
        if i >= TEMP_ARC["n"] // 2:          # thin red line under the hot half
            cv.sector("x", 400, 400, TEMP_ARC["r_in"] - 5, TEMP_ARC["r_in"] - 8, a, b)
    rm = (TEMP_ARC["r_out"] + TEMP_ARC["r_in"]) / 2
    for txt, a, ink in (("C", TEMP_ARC["a0"] - 6, "r"), ("H", TEMP_ARC["a1"] + 6, "x")):
        cv.text_center(ink, txt, 15, 400 + rm * math.sin(math.radians(a)), 400 - rm * math.cos(math.radians(a)) + 6)
    ic_thermo(cv, 400, 246, 60)
    cv.text("r", "%", 12, 400 + 84, 152, anchor="l")
    plusminus(cv, *TEMP_MINUS, False, pressed == "temp_minus")
    plusminus(cv, *TEMP_PLUS, True, pressed == "temp_plus")

    # mode row
    cv.text("dim", "MODE", 11, 400, MODE_Y - 32)
    for (key, name), (x0, y0, x1, y1) in zip(MODES, mode_boxes()):
        active = key == mode
        if active:
            cv.rrect("r", x0, y0, x1, y1, 16, fill=True)
        else:
            cv.rrect("w", x0, y0, x1, y1, 16, 2.0)
        draw_mode_icon(cv, key, (x0 + x1) / 2, (y0 + y1) / 2, 74, "k" if active else "w", "k" if active else "r")
        cv.text("r" if active else "dim", name, 9, (x0 + x1) / 2, y1 + 14, max_w=MODE_BTN + 6)

    # fan bar + ends
    cv.arc_track("seg", 400, 400, (FAN_ARC["r_out"] + FAN_ARC["r_in"]) / 2, FAN_ARC["r_out"] - FAN_ARC["r_in"], min(FAN_ARC["a0"], FAN_ARC["a1"]), max(FAN_ARC["a0"], FAN_ARC["a1"]))
    for a, b in segs_of(FAN_ARC):
        pass
    for txt, a in (("OFF", FAN_ARC["a0"] + 8), ("MAX", FAN_ARC["a1"] - 8)):
        cv.text_center("w", txt, 11, 400 + rm * math.sin(math.radians(a)), 400 - rm * math.cos(math.radians(a)) - 2)
    ic_fan(cv, 400, 556, 58)
    plusminus(cv, *FAN_MINUS, False, pressed == "fan_minus")
    plusminus(cv, *FAN_PLUS, True, pressed == "fan_plus")
    return cv.render()


def to_lvgl(d12):
    return round((d12 + 270) % 360, 2)


def preview(img, temp_lit, fan_lit, temp_txt, fan_txt):
    cv = Canvas(D, SS=3)
    big = img.resize((cv.S, cv.S), Image.LANCZOS)
    for i, (a, b) in enumerate(segs_of(TEMP_ARC)[:temp_lit]):
        cv.sector("x" if i >= TEMP_ARC["n"] // 2 else "r", 400, 400,
                  TEMP_ARC["r_out"], TEMP_ARC["r_in"], a, b)
    for i, (a, b) in enumerate(segs_of(FAN_ARC)[:fan_lit]):
        cv.sector("r", 400, 400, FAN_ARC["r_out"], FAN_ARC["r_in"], a, b)
    for txt, (x, y, w, h) in ((temp_txt, TEMP_VALUE), (fan_txt, FAN_VALUE)):
        g = cv.text_mask(txt, 50, NUM_SQUEEZE, weight=NUMBER_WEIGHT)
        cv.m["w"].paste(255, (int(cv.s(x + w / 2) - g.width / 2), int(cv.s(y + h / 2) - g.height / 2)), g)
    for k, col in (("w", INK_WHITE), ("r", INK_RED), ("x", INK_WARN)):
        big = Image.composite(Image.new("RGB", big.size, col), big, cv.m[k].filter(ImageFilter.GaussianBlur(1)))
    return big.resize((D, D), Image.LANCZOS)


def main():
    os.makedirs(os.path.join(OUT_DIR, "tiles"), exist_ok=True)
    os.makedirs(os.path.join(OUT_DIR, "previews"), exist_ok=True)
    bg = climate_page()
    bg.save(os.path.join(OUT_DIR, "climate_800_bg.png"))

    mode_layout = []
    for (key, name), (x0, y0, x1, y1) in zip(MODES, mode_boxes()):
        box = (x0 - 4, y0 - 4, x1 + 4, y1 + 34)          # includes the label under the button
        img = climate_page(mode=key)
        img.crop(box).save(os.path.join(OUT_DIR, "tiles", "mode_%s_on.png" % key))
        mode_layout.append(dict(id=key, name=name, touch_box=[x0, y0, x1, y1 + 30], tile_xy=list(box[:2]),
                                tile_size=[box[2] - box[0], box[3] - box[1]], tile_on="tiles/mode_%s_on.png" % key,
                                tile_off_note="crop the background at tile_xy to restore"))
    btns = {}
    for key, (cx, cy) in (("temp_minus", TEMP_MINUS), ("temp_plus", TEMP_PLUS),
                          ("fan_minus", FAN_MINUS), ("fan_plus", FAN_PLUS)):
        box = (cx - BTN_R - 4, cy - BTN_R - 4, cx + BTN_R + 4, cy + BTN_R + 4)
        climate_page(pressed=key).crop(box).save(os.path.join(OUT_DIR, "tiles", "pressed_%s.png" % key))
        btns[key] = dict(touch_box=list(box), pressed_tile="tiles/pressed_%s.png" % key)
    # "off" tiles for modes so firmware can restore without re-reading the background
    for (key, _), m in zip(MODES, mode_layout):
        x, y = m["tile_xy"]
        w, h = m["tile_size"]
        bg.crop((x, y, x + w, y + h)).save(os.path.join(OUT_DIR, "tiles", "mode_%s_off.png" % key))
        m["tile_off"] = "tiles/mode_%s_off.png" % key
        del m["tile_off_note"]

    layout = dict(
        background="climate_800_bg.png", size=[D, D],
        temp=dict(value_box=dict(zip("xywh", TEMP_VALUE)), range=[0, 100], step=5, unit="%",
                  ring=dict(center=[400, 400], radius_outer=TEMP_ARC["r_out"], width=TEMP_ARC["r_out"] - TEMP_ARC["r_in"],
                            segments=[dict(start=to_lvgl(a), end=to_lvgl(b)) for a, b in segs_of(TEMP_ARC)],
                            red_from_segment=TEMP_ARC["n"] // 2)),
        fan=dict(value_box=dict(zip("xywh", FAN_VALUE)), range=[0, 4], step=1,
                 ring=dict(center=[400, 400], radius_outer=FAN_ARC["r_out"], width=FAN_ARC["r_out"] - FAN_ARC["r_in"],
                           segments=[dict(start=to_lvgl(a), end=to_lvgl(b)) for a, b in segs_of(FAN_ARC)][::-1],
                           order="segment 0 = left (OFF side)")),
        mode=mode_layout, buttons=btns)
    # fan segments: segs_of sorts each pair; list left->right for LVGL
    fan_lr = sorted(segs_of(FAN_ARC), key=lambda ab: -ab[0])
    layout["fan"]["ring"]["segments"] = [dict(start=to_lvgl(a), end=to_lvgl(b)) for a, b in fan_lr]
    with open(os.path.join(OUT_DIR, "climate_layout.json"), "w") as f:
        json.dump(layout, f, indent=2)

    preview(climate_page(mode="bilevel"), 12, 2, "60", "2").save(os.path.join(OUT_DIR, "previews", "climate_bilevel_60_fan2.png"))
    preview(climate_page(mode="defrost", pressed="fan_plus"), 4, 4, "20", "4").save(
        os.path.join(OUT_DIR, "previews", "climate_defrost_20_fan4_pressed.png"))
    print("done")


if __name__ == "__main__":
    main()
