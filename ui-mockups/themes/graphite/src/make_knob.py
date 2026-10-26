"""
DigiDash remote knob UI - 240x240 ROUND (1.28" ESP32 rotary display) - GRAPHITE theme.
Same pages, file names and coordinates as the classic theme; flat face, DIN lettering, ice-blue accent.

Every menu page is a finished 240x240 image (icon + name + header + position marker).
Adjust pages are backgrounds with an unlit segmented ring; LVGL lights segments and draws the value.

Run:  python make_knob.py      ->  knob_ui/*.png, knob_ui/knob_layout.json, knob_ui_contact_sheet.png
"""
import json
import math
import os
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops

D = 240
SS = 4
S = D * SS
C = S / 2
OUT_DIR = "knob_ui"

import theme
INK_WHITE = theme.TEXT
INK_RED = theme.ACCENT          # kept as "r" so the classic code maps 1:1; it is the accent colour here
INK_WARN = theme.WARN
INK_DIM = theme.TEXT2
SEG_OFF = theme.TRACK
MARK_OFF = (54, 59, 66)

# ring used by adjust pages (same 260 deg sweep as the tach)
RING_R_OUT, RING_R_IN = 113, 102
RING_START, RING_SWEEP = -130.0, 260.0
RING_GAP = 0.0                  # GRAPHITE: one continuous track

# page-position marker (bottom of the screen)
MARK_R_OUT, MARK_R_IN = 112, 107
MARK_SEG, MARK_GAP = 7.0, 3.0


def s(v):
    return v * SS


def polar(r, deg, cx=120, cy=120):
    a = math.radians(deg)
    return s(cx + r * math.sin(a)), s(cy - r * math.cos(a))


def sector(d, r_out, r_in, a0, a1, fill=255, steps=60):
    pts = [polar(r_out, a0 + (a1 - a0) * i / steps) for i in range(steps + 1)]
    pts += [polar(r_in, a1 - (a1 - a0) * i / steps) for i in range(steps + 1)]
    d.polygon(pts, fill=fill)


def to_lvgl(d12):
    return round((d12 + 270) % 360, 2)


# ---------------------------------------------------------------- text
def font_for(cap, weight=None):
    return theme.font(max(4, int(s(cap) * 1.45)), weight or theme.LABEL_WEIGHT)


def text_mask(txt, cap, weight=None):
    f = font_for(cap, weight)
    l, t, r, b = f.getbbox(txt)
    pad = s(6)
    m = Image.new("L", (r - l + 2 * pad, b - t + 2 * pad), 0)
    ImageDraw.Draw(m).text((pad - l, pad - t), txt, font=f, fill=255)
    m = m.crop(m.getbbox())
    hb = f.getbbox("H")
    k = s(cap) / (hb[3] - hb[1])
    return m.resize((max(1, int(m.width * k)), max(1, int(m.height * k))), Image.LANCZOS)


def put_text(mask, txt, cap, cx, y, max_w=None, weight=None):
    """Centred at cx, capitals top at y. Shrinks to fit max_w (screen px)."""
    if max_w:
        while cap > 6 and text_mask(txt, cap, weight).width / SS > max_w:
            cap -= 0.5
    g = text_mask(txt, cap, weight)
    f = font_for(cap, weight)
    hb, tb = f.getbbox("H"), f.getbbox(txt)
    k = s(cap) / (hb[3] - hb[1])
    mask.paste(255, (int(s(cx) - g.width / 2), int(s(y) + (tb[1] - hb[1]) * k)), g)


# ---------------------------------------------------------------- face
def base_face():
    return theme.base_face(D, SS)


class Layers:
    """White / red / dim / unlit-segment masks, composited onto the face at the end."""
    ORDER = (("seg", SEG_OFF), ("mark", MARK_OFF), ("dim", INK_DIM), ("w", INK_WHITE), ("r", INK_RED), ("x", INK_WARN))

    def __init__(self):
        self.m = {k: Image.new("L", (S, S), 0) for k, _ in self.ORDER}
        self.d = {k: ImageDraw.Draw(v) for k, v in self.m.items()}

    def render(self):
        face = base_face()
        soft = lambda m: m.filter(ImageFilter.GaussianBlur(SS * 0.3))
        for k, col in self.ORDER:
            face = Image.composite(Image.new("RGB", face.size, col), face, soft(self.m[k]))
        mask = Image.new("L", face.size, 0)
        ImageDraw.Draw(mask).ellipse([0, 0, S - 1, S - 1], fill=255)
        img = Image.composite(face, Image.new("RGB", face.size, (0, 0, 0)), mask)
        return img.resize((D, D), Image.LANCZOS)


# ---------------------------------------------------------------- icon primitives
class Pen:
    """Draws in a 100x100 icon box centred at (cx, cy) with size `size` (screen px)."""

    def __init__(self, L, cx, cy, size):
        self.L, self.cx, self.cy, self.u = L, cx, cy, size / 100.0

    def P(self, x, y):
        return s(self.cx + x * self.u), s(self.cy + y * self.u)

    def W(self, w):
        return max(1, int(s(w * self.u)))

    def line(self, pts, w=6, ink="w"):
        self.L.d[ink].line([self.P(*p) for p in pts], fill=255, width=self.W(w), joint="curve")
        for p in (pts[0], pts[-1]):                       # round caps
            self.dot(p[0], p[1], w / 2, ink)

    def dot(self, x, y, r, ink="w"):
        X, Y = self.P(x, y)
        R = s(r * self.u)
        self.L.d[ink].ellipse([X - R, Y - R, X + R, Y + R], fill=255)

    def ring(self, x, y, r, w=6, ink="w"):
        X, Y = self.P(x, y)
        R = s(r * self.u)
        self.L.d[ink].ellipse([X - R, Y - R, X + R, Y + R], outline=255, width=self.W(w))

    def rrect(self, x0, y0, x1, y1, rad, w=6, ink="w", fill=False):
        box = [*self.P(x0, y0), *self.P(x1, y1)]
        if fill:
            self.L.d[ink].rounded_rectangle(box, radius=s(rad * self.u), fill=255)
        else:
            self.L.d[ink].rounded_rectangle(box, radius=s(rad * self.u), outline=255, width=self.W(w))

    def poly(self, pts, ink="w"):
        self.L.d[ink].polygon([self.P(*p) for p in pts], fill=255)

    def arc(self, x, y, r, a0, a1, w=6, ink="w"):
        """a0..a1 in degrees from 12 o'clock, clockwise."""
        pts = [(x + r * math.sin(math.radians(a0 + (a1 - a0) * i / 40)),
                y - r * math.cos(math.radians(a0 + (a1 - a0) * i / 40))) for i in range(41)]
        self.line(pts, w, ink)

    def arrow(self, x0, y0, x1, y1, w=6, head=11, ink="w"):
        a = math.atan2(y1 - y0, x1 - x0)
        bx, by = x1 - head * 0.8 * math.cos(a), y1 - head * 0.8 * math.sin(a)
        self.line([(x0, y0), (bx, by)], w, ink)
        px, py = -math.sin(a), math.cos(a)
        self.poly([(x1, y1), (bx - head * 0.25 * math.cos(a) + px * head * 0.62, by - head * 0.25 * math.sin(a) + py * head * 0.62),
                   (bx - head * 0.25 * math.cos(a) - px * head * 0.62, by - head * 0.25 * math.sin(a) - py * head * 0.62)], ink)

    def wave_up(self, x, y0, y1, amp=3.5, w=5, ink="w"):
        n = 24
        pts = [(x + amp * math.sin(i / n * math.pi * 2.2), y0 + (y1 - y0) * i / n) for i in range(n + 1)]
        self.line(pts[:-3], w, ink)
        self.arrow(pts[-4][0], pts[-4][1], pts[-1][0], pts[-1][1] - 2, w, 10, ink)


# ---------------------------------------------------------------- icons
def ic_gauge(p, red_zone=False, ticks=9, ink="w"):
    p.arc(0, 4, 40, -130, 130, 6, ink)
    for i in range(ticks):
        a = math.radians(-130 + 260 * i / (ticks - 1))
        r0, r1 = 40, 31
        p.line([(r0 * math.sin(a), 4 - r0 * math.cos(a)), (r1 * math.sin(a), 4 - r1 * math.cos(a))], 4.5, ink)
    if red_zone:
        p.arc(0, 4, 40, 92, 130, 9, "r")
    a = math.radians(-35)
    p.line([(0, 4), (30 * math.sin(a), 4 - 30 * math.cos(a))], 6, "r")
    p.dot(0, 4, 7, ink)


def ic_peak(p):
    """A summit with a flag on top."""
    p.line([(-42, 30), (-14, -16), (0, 4), (16, -32), (42, 30)], 6)
    p.line([(16, -32), (16, -48)], 4.5, "r")
    p.poly([(16, -48), (34, -42), (16, -36)], "r")


def ic_stopwatch(p):
    """Stopwatch with the hand mid-sweep."""
    p.ring(0, 8, 34, 6)
    p.rrect(-9, -44, 9, -34, 3, 5, fill=True)
    p.line([(0, -34), (0, -26)], 5)
    p.line([(0, 8), (17, -9)], 6, "r")
    p.dot(0, 8, 6)


def ic_grid(p):
    """Six value tiles in two columns, one highlighted like a live reading."""
    for row in range(3):
        for col in range(2):
            x0, y0 = -38 + col * 40, -38 + row * 27
            p.rrect(x0, y0, x0 + 34, y0 + 21, 5, 5.5, "r" if (row, col) == (0, 0) else "w")


def ic_screens(p, sel=None):
    """Three round gauges side by side; sel = 0/1/2 highlights one in red."""
    for i, (x, y, r) in enumerate(((-37, 6, 14), (0, 0, 21), (37, 6, 14))):
        ink = "r" if sel == i else "w"
        p.ring(x, y, r, 5.5 if sel != i else 7, ink)
        a = math.radians(-50)
        p.line([(x, y), (x + r * 0.62 * math.sin(a), y - r * 0.62 * math.cos(a))], 4.5, ink)
    if sel is None:
        a = math.radians(-50)
        p.line([(0, 0), (13 * math.sin(a), -13 * math.cos(a))], 5, "r")


def ic_rec(p, active=False):
    if active:
        p.ring(0, 0, 38, 5, "x")
        p.rrect(-15, -15, 15, 15, 4, ink="x", fill=True)
    else:
        p.ring(0, 0, 38, 4.5, "w")
        p.dot(0, 0, 22, "x")


def ic_airbag(p):
    # top plate, two bellows lobes, piston, up/down arrows
    p.line([(-30, -38), (18, -38)], 6)
    p.rrect(-28, -32, 16, -10, 11)
    p.rrect(-28, -10, 16, 12, 11)
    p.line([(-14, 12), (-14, 26)], 6)
    p.line([(2, 12), (2, 26)], 6)
    p.line([(-24, 28), (12, 28)], 6)
    p.line([(-6, 30), (-6, 42)], 7)
    p.arrow(36, -2, 36, -36, 5.5, 12, "r")
    p.arrow(36, 6, 36, 40, 5.5, 12, "r")


def ic_fan(L, cx, cy, size, ink="w"):
    """4-blade fan, blades are rotated ellipses."""
    u = size / 100
    m = Image.new("L", (S, S), 0)
    for k in range(4):
        b = Image.new("L", (S, S), 0)
        bd = ImageDraw.Draw(b)
        bx, by = cx + 9 * u, cy - 23 * u
        bd.ellipse([s(bx - 11 * u), s(by - 21 * u), s(bx + 11 * u), s(by + 21 * u)], fill=255)
        b = b.rotate(-(k * 90 + 18), resample=Image.BICUBIC, center=(s(cx), s(cy)))
        m = ImageChops.lighter(m, b)
    hub = Image.new("L", (S, S), 0)
    ImageDraw.Draw(hub).ellipse([s(cx - 9 * u), s(cy - 9 * u), s(cx + 9 * u), s(cy + 9 * u)], fill=255)
    m = ImageChops.subtract(m, hub)
    ImageDraw.Draw(m).ellipse([s(cx - 5 * u), s(cy - 5 * u), s(cx + 5 * u), s(cy + 5 * u)], fill=255)
    L.m[ink].paste(255, (0, 0), m)


def ic_thermo(p, fill_level=0.55, x=0, sc=1.0):
    q = sc
    p.rrect(x - 8 * q, -40 * q, x + 8 * q, 16 * q, 8 * q, 5.5 * q)
    p.ring(x, 26 * q, 14 * q, 5.5 * q)
    p.dot(x, 26 * q, 8 * q, "r")
    top = 16 * q - (52 * q) * fill_level
    p.line([(x, 20 * q), (x, top)], 6 * q, "r")
    for i in range(3):
        p.line([(x + 14 * q, (-30 + i * 11) * q), (x + 22 * q, (-30 + i * 11) * q)], 4 * q)


def ic_climate(L, p, cx, cy, size):
    ic_fan(L, cx - size * 0.1, cy - size * 0.04, size * 0.78)
    pp = Pen(L, cx + size * 0.33, cy + size * 0.12, size * 0.5)
    ic_thermo(pp, 0.6)


def ic_logs(p):
    p.line([(-28, -40), (12, -40), (28, -24), (28, 40), (-28, 40), (-28, -40)], 6)
    p.line([(12, -40), (12, -24), (28, -24)], 5)
    p.line([(-16, -20), (2, -20)], 5)
    p.line([(-16, -8), (14, -8)], 5)
    p.line([(-16, 28), (-5, 12), (5, 19), (16, 3)], 6, "r")


def ic_sun(p, big=True):
    p.dot(0, 0, 15 if big else 11)
    n = 8
    for i in range(n):
        a = math.radians(i * 360 / n)
        r0, r1 = (25, 38) if big else (19, 26)
        p.line([(r0 * math.sin(a), -r0 * math.cos(a)), (r1 * math.sin(a), -r1 * math.cos(a))], 6 if big else 5)


def ic_car(p, sel=()):
    """Top view, front up. sel subset of {'fl','fr','rl','rr'} fills those wheels red."""
    p.rrect(-17, -40, 17, 40, 12, 5.5)
    p.line([(-12, -16), (12, -16)], 4.5)              # windshield
    p.line([(-12, 18), (12, 18)], 4.5)                # rear window
    for key, x, y in (("fl", -27, -24), ("fr", 27, -24), ("rl", -27, 24), ("rr", 27, 24)):
        if key in sel:
            p.rrect(x - 7, y - 12, x + 7, y + 12, 4, ink="r", fill=True)
        else:
            p.rrect(x - 6, y - 11, x + 6, y + 11, 4, 4.5)
    p.poly([(-7, -52), (7, -52), (0, -59)], "dim")    # front marker


def ic_back(p):
    p.arc(4, 4, 24, 200, 450, 6.5)
    p.arrow(-16, -2, -22, -26, 6.5, 14)


def person(p):
    p.dot(-14, -26, 8)
    p.line([(-18, -14), (-20, 10), (8, 10), (10, 34)], 7)


def ic_mode(p, face=False, feet=False, defrost=False):
    person(p)
    if face:
        p.arrow(40, -26, 6, -26, 5.5, 12, "r")
    if feet:
        p.arrow(40, 34, 22, 34, 5.5, 12, "r")
    if defrost:
        p.line([(10, -48), (40, -26)], 6)              # windshield, seen from the side
        p.wave_up(27, 2, -28, 3, 4.5, "r")


def ic_defrost(p):
    p.line([(-38, 4), (38, 4), (29, -30), (-29, -30), (-38, 4)], 6)
    for x in (-16, 0, 16):
        p.wave_up(x, 34, 4, 3.2, 4.5, "r")


ICON_FUNCS = {
    "screens": lambda L, p, *a: ic_screens(p),
    "screen0": lambda L, p, *a: ic_screens(p, 0),
    "screen1": lambda L, p, *a: ic_screens(p, 1),
    "screen2": lambda L, p, *a: ic_screens(p, 2),
    "rec": lambda L, p, *a: ic_rec(p),
    "rec_active": lambda L, p, *a: ic_rec(p, True),
    "air": lambda L, p, *a: ic_airbag(p),
    "climate": lambda L, p, cx, cy, size: ic_climate(L, p, cx, cy, size),
    "fan": lambda L, p, cx, cy, size: ic_fan(L, cx, cy, size * 0.95),
    "temp": lambda L, p, *a: ic_thermo(p, 0.6),
    "mode": lambda L, p, *a: ic_mode(p, face=True, feet=True),
    "logs": lambda L, p, *a: ic_logs(p),
    "bright": lambda L, p, *a: ic_sun(p),
    "tach": lambda L, p, *a: ic_gauge(p, True),
    "speedo": lambda L, p, *a: ic_gauge(p, False, 11),
    "data": lambda L, p, *a: ic_thermo(p, 0.5),
    "gauges": lambda L, p, *a: ic_grid(p),
    "peaks": lambda L, p, *a: ic_peak(p),
    "timer": lambda L, p, *a: ic_stopwatch(p),
    "car_all": lambda L, p, *a: ic_car(p, ("fl", "fr", "rl", "rr")),
    "car_front": lambda L, p, *a: ic_car(p, ("fl", "fr")),
    "car_rear": lambda L, p, *a: ic_car(p, ("rl", "rr")),
    "car_fl": lambda L, p, *a: ic_car(p, ("fl",)),
    "car_fr": lambda L, p, *a: ic_car(p, ("fr",)),
    "car_rl": lambda L, p, *a: ic_car(p, ("rl",)),
    "car_rr": lambda L, p, *a: ic_car(p, ("rr",)),
    "mode_face": lambda L, p, *a: ic_mode(p, face=True),
    "mode_bilevel": lambda L, p, *a: ic_mode(p, face=True, feet=True),
    "mode_feet": lambda L, p, *a: ic_mode(p, feet=True),
    "mode_feetdef": lambda L, p, *a: ic_mode(p, feet=True, defrost=True),
    "mode_defrost": lambda L, p, *a: ic_defrost(p),
    "back": lambda L, p, *a: ic_back(p),
}


def draw_icon(L, name, cx, cy, size):
    ICON_FUNCS[name](L, Pen(L, cx, cy, size), cx, cy, size)


# ---------------------------------------------------------------- page types
def position_marker(L, idx, count):
    span = count * MARK_SEG + (count - 1) * MARK_GAP
    a0 = 180 + span / 2                                # left end (clockwise angles, bottom = 180)
    out = []
    for i in range(count):                             # GRAPHITE: dots, active one larger in accent
        b = a0 - i * (MARK_SEG + MARK_GAP)
        a = b - MARK_SEG
        x, y = polar((MARK_R_OUT + MARK_R_IN) / 2, (a + b) / 2)
        R = s(3.3 if i == idx else 2.3)
        L.d["r" if i == idx else "mark"].ellipse([x - R, y - R, x + R, y + R], fill=255)
        out.append((a, b))
    return out


def side_chevrons(L):
    for sgn in (-1, 1):
        x = 120 + sgn * 104
        pts = [(x - sgn * 4, 112), (x + sgn * 2, 120), (x - sgn * 4, 128)]
        L.d["mark"].line([(s(a), s(b)) for a, b in pts], fill=255, width=int(s(2.4)), joint="curve")


def menu_page(header, icon, name, idx, count, name_ink="w", header_ink="dim"):
    L = Layers()
    if header:
        put_text(L.m[header_ink], header, 7.5, 120, 34, max_w=120)
    draw_icon(L, icon, 120, 100, 84)
    put_text(L.m[name_ink], name, 13, 120, 160, max_w=176)
    position_marker(L, idx, count)
    side_chevrons(L)
    return L.render()


def ring_segments(n):
    seg = (RING_SWEEP - RING_GAP * (n - 1)) / n
    return [(RING_START + i * (seg + RING_GAP), RING_START + i * (seg + RING_GAP) + seg) for i in range(n)]


def adjust_page(title, icon, n_segs, lo_txt, hi_txt, unit, lo_ink="w", hi_ink="w", lo_icon=None, hi_icon=None):
    L = Layers()
    segs = ring_segments(n_segs)
    sector(L.d["seg"], RING_R_OUT, RING_R_IN, segs[0][0], segs[-1][1], steps=200)
    for a_end in (segs[0][0], segs[-1][1]):            # rounded track ends
        x, y = polar((RING_R_OUT + RING_R_IN) / 2, a_end)
        R = s((RING_R_OUT - RING_R_IN) / 2)
        L.d["seg"].ellipse([x - R, y - R, x + R, y + R], fill=255)
    draw_icon(L, icon, 120, 50, 34)
    put_text(L.m["dim"], title, 7.5, 120, 74, max_w=130)
    if unit:
        put_text(L.m["r"], unit, 9, 120, 150)
    for txt, ic, deg, ink in ((lo_txt, lo_icon, RING_START - 2, lo_ink), (hi_txt, hi_icon, -RING_START + 2, hi_ink)):
        x, y = polar(88, deg)
        x, y = x / SS, y / SS + 14
        if ic:
            draw_icon(L, ic, x, y, 12 if deg < 0 else 20)
        elif txt:
            put_text(L.m[ink], txt, 8, x, y - 4)
    layout = dict(value_box=dict(x=40, y=96, w=160, h=48), value_cap_px=32,
                  ring=dict(radius_outer=RING_R_OUT, width=RING_R_OUT - RING_R_IN,
                            segments=[dict(start=to_lvgl(a), end=to_lvgl(b)) for a, b in segs]))
    return L.render(), layout


# ---------------------------------------------------------------- the menu tree
MAIN = [
    ("screen", "screens", "SCREEN"),
    ("rec", "rec", "REC"),
    ("air", "air", "AIR RIDE"),
    ("climate", "climate", "CLIMATE"),
    ("logs", "logs", "LOGS"),
    ("bright", "bright", "BRIGHTNESS"),
]

SUBMENUS = {
    # key: (header, [(id, icon, name)])
    "screen_target": ("SCREEN", [("left", "screen0", "LEFT"), ("center", "screen1", "CENTER"),
                                 ("right", "screen2", "RIGHT"), ("back", "back", "BACK")]),
    "air": ("AIR RIDE", [("all", "car_all", "ALL"), ("front", "car_front", "FRONT"), ("rear", "car_rear", "REAR"),
                         ("fl", "car_fl", "FRONT LEFT"), ("fr", "car_fr", "FRONT RIGHT"),
                         ("rl", "car_rl", "REAR LEFT"), ("rr", "car_rr", "REAR RIGHT"), ("back", "back", "BACK")]),
    "climate": ("CLIMATE", [("fan", "fan", "FAN"), ("temp", "temp", "TEMP"), ("mode", "mode", "MODE"),
                            ("back", "back", "BACK")]),
    "mode": ("MODE", [("face", "mode_face", "FACE"), ("bilevel", "mode_bilevel", "BI-LEVEL"),
                      ("feet", "mode_feet", "FEET"), ("feetdef", "mode_feetdef", "FEET/DEF"),
                      ("defrost", "mode_defrost", "DEFROST"), ("back", "back", "BACK")]),
    "logs": ("OPEN LOGS ON", [("left", "screen0", "LEFT"), ("center", "screen1", "CENTER"),
                              ("right", "screen2", "RIGHT"), ("back", "back", "BACK")]),
}


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    pages = []                                     # (filename, image) for the contact sheet
    layout = {"screen": [D, D], "round": True,
              "marker": dict(radius_outer=MARK_R_OUT, width=MARK_R_OUT - MARK_R_IN,
                             note="baked into every menu page; listed for reference"),
              "menus": {}, "adjust": {}}

    def save(name, img):
        img.save(os.path.join(OUT_DIR, name + ".png"))
        pages.append((name, img))

    layout["menus"]["main"] = []
    for i, (key, icon, name) in enumerate(MAIN):
        fn = "main_%d_%s" % (i, key)
        save(fn, menu_page(None, icon, name, i, len(MAIN)))
        layout["menus"]["main"].append(dict(id=key, image=fn))

    for menu, (header, items) in SUBMENUS.items():
        layout["menus"][menu] = []
        for i, (key, icon, name) in enumerate(items):
            fn = "%s_%d_%s" % (menu, i, key)
            save(fn, menu_page(header, icon, name, i, len(items)))
            layout["menus"][menu].append(dict(id=key, image=fn))

    source_menus = {
        "left": [("data", "data", "DATA"), ("gauges", "gauges", "GAUGES"),
                 ("peaks", "peaks", "PEAKS"),
                 ("timer", "timer", "TIMER"), ("speedo", "speedo", "SPEEDO"),
                 ("air", "air", "AIR RIDE"), ("climate", "climate", "CLIMATE"),
                 ("back", "back", "BACK")],
        "center": [("tach", "tach", "TACH"), ("air", "air", "AIR RIDE"),
                   ("logs", "logs", "LOGS"), ("back", "back", "BACK")],
        "right": [("data", "data", "DATA"), ("gauges", "gauges", "GAUGES"),
                  ("peaks", "peaks", "PEAKS"),
                  ("timer", "timer", "TIMER"), ("speedo", "speedo", "SPEEDO"),
                  ("air", "air", "AIR RIDE"), ("climate", "climate", "CLIMATE"),
                  ("back", "back", "BACK")],
    }
    layout["menus"]["screen_source"] = {}
    generated_sources = set()
    for target, items in source_menus.items():
        layout["menus"]["screen_source"][target] = []
        for i, (key, icon, name) in enumerate(items):
            fn = "screen_source_%d_%s" % (i, key)
            if fn not in generated_sources:
                save(fn, menu_page("SHOW ON SCREEN", icon, name, i, len(items)))
                generated_sources.add(fn)
            layout["menus"]["screen_source"][target].append(dict(id=key, image=fn))

    # recording page (two states, no marker: press toggles)
    for key, icon, name, hdr_ink in (("rec_idle", "rec", "START", "dim"), ("rec_active", "rec_active", "STOP", "x")):
        L = Layers()
        put_text(L.m[hdr_ink], "RECORDING" if key == "rec_active" else "DATA LOG", 7.5, 120, 34)
        draw_icon(L, icon, 120, 100, 84)
        put_text(L.m["w"], name, 13, 120, 160)
        put_text(L.m["dim"], "HOLD TO EXIT", 6, 120, 200)
        save(key, L.render())
    layout["rec"] = dict(timer_box=dict(x=60, y=178, w=120, h=16), timer_cap_px=10,
                         note="LVGL draws elapsed time mm:ss under STOP while recording")

    # adjust pages
    adj = {}
    for key, title, icon in (("fl", "FRONT LEFT", "car_fl"), ("fr", "FRONT RIGHT", "car_fr"),
                             ("rl", "REAR LEFT", "car_rl"), ("rr", "REAR RIGHT", "car_rr"),
                             ("front", "FRONT", "car_front"), ("rear", "REAR", "car_rear"), ("all", "ALL", "car_all")):
        img, lay = adjust_page(title, icon, 20, "0", "150", "PSI")
        save("adjust_air_" + key, img)
        adj["air_" + key] = dict(lay, range=[0, 150], unit="PSI")
    img, lay = adjust_page("FAN", "fan", 4, "OFF", "MAX", "SPEED")
    save("adjust_fan", img)
    adj["fan"] = dict(lay, range=[0, 4])
    img, lay = adjust_page("TEMP", "temp", 20, "C", "H", None, lo_ink="r", hi_ink="x")
    save("adjust_temp", img)
    adj["temp"] = dict(lay, range=[0, 100], note="segments 0-9 white, 10-19 red; value is blend door %")
    img, lay = adjust_page("BRIGHTNESS", "bright", 20, None, None, "%", lo_icon="bright", hi_icon="bright")
    save("adjust_bright", img)
    adj["bright"] = dict(lay, range=[5, 100], unit="%")
    layout["adjust"] = adj

    # previews with sample values (what the finished LVGL screen should look like)
    os.makedirs("knob_ui_previews", exist_ok=True)
    samples = dict(air_fl=("82", 11, None), air_all=("80", 11, None), fan=("2", 2, None),
                   temp=("60", 12, 10), bright=("70", 14, None))
    for key, (txt, lit, red_from) in samples.items():
        base = Image.open(os.path.join(OUT_DIR, "adjust_" + key + ".png")).resize((S, S), Image.LANCZOS)
        w, r, x = Image.new("L", (S, S), 0), Image.new("L", (S, S), 0), Image.new("L", (S, S), 0)
        segs = adj[key]["ring"]["segments"]
        for i in range(lit):
            a = (segs[i]["start"] - 270) % 360
            b = (segs[i]["end"] - 270) % 360
            a, b = (a - 360 if a > 180 else a), (b - 360 if b > 180 else b)
            sector(ImageDraw.Draw(x if (red_from is not None and i >= red_from) else r),
                   RING_R_OUT, RING_R_IN, a, b)
        put_text(w, txt, 26, 120, 107, weight=theme.NUMBER_WEIGHT)
        img = Image.composite(Image.new("RGB", (S, S), INK_WHITE), base, w.filter(ImageFilter.GaussianBlur(SS * 0.3)))
        img = Image.composite(Image.new("RGB", (S, S), INK_RED), img, r.filter(ImageFilter.GaussianBlur(SS * 0.3)))
        img = Image.composite(Image.new("RGB", (S, S), INK_WARN), img, x.filter(ImageFilter.GaussianBlur(SS * 0.3)))
        img.resize((D, D), Image.LANCZOS).save(os.path.join("knob_ui_previews", "preview_adjust_" + key + ".png"))
    base = Image.open(os.path.join(OUT_DIR, "rec_active.png")).resize((S, S), Image.LANCZOS)
    w = Image.new("L", (S, S), 0)
    put_text(w, "03:27", 10, 120, 180, weight=theme.NUMBER_WEIGHT)
    img = Image.composite(Image.new("RGB", (S, S), INK_WARN), base, w.filter(ImageFilter.GaussianBlur(SS * 0.3)))
    img.resize((D, D), Image.LANCZOS).save(os.path.join("knob_ui_previews", "preview_rec_active.png"))

    with open(os.path.join(OUT_DIR, "knob_layout.json"), "w") as f:
        json.dump(layout, f, indent=2)

    # contact sheet
    cols = 8
    rows = math.ceil(len(pages) / cols)
    cell_w, cell_h = 250, 268
    sheet = Image.new("RGB", (cols * cell_w + 10, rows * cell_h + 10), (12, 12, 13))
    sd = ImageDraw.Draw(sheet)
    lab = ImageFont.truetype("C:/Windows/Fonts/consola.ttf", 13)
    for i, (name, img) in enumerate(pages):
        x, y = 10 + (i % cols) * cell_w, 10 + (i // cols) * cell_h
        sheet.paste(img, (x, y))
        sd.text((x, y + 243), name, font=lab, fill=(170, 170, 170))
    sheet.save("knob_ui_contact_sheet.png")
    print(len(pages), "pages")


if __name__ == "__main__":
    main()
