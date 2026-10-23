"""
GRAPHITE theme - shared palette, font and drawing canvas for every DigiDash screen.

Modern and quiet: flat graphite face with a soft radial falloff (no texture), thin rounded arc tracks,
light DIN-style digits, one calm accent colour, red only for warnings.

Change the palette here and re-run the generators to re-colour everything.
"""
import math
import os
from functools import lru_cache
from PIL import Image, ImageDraw, ImageFont, ImageFilter

# ---------------------------------------------------------------- palette
BG_CENTER = (25, 27, 31)
BG_EDGE = (9, 10, 12)
TEXT = (238, 240, 242)          # primary text / lit white
TEXT2 = (134, 141, 151)         # labels, scale numbers
TRACK = (38, 42, 48)            # unlit arc tracks, idle outlines
HAIR = (30, 33, 38)             # hairlines / dividers
ACCENT = (94, 200, 242)         # ice blue: active, selected, normal fill
WARN = (255, 86, 74)            # red: redline, hot, air out, alarms
ON_FILL = (12, 14, 17)          # text/icons on a filled accent button

HEX = lambda c: "#%02X%02X%02X" % c

# ---------------------------------------------------------------- font
FONT = "C:/Windows/Fonts/bahnschrift.ttf"     # variable: weight 300-700, width 75-100
LABEL_WEIGHT = 600
NUMBER_WEIGHT = 330


@lru_cache(maxsize=256)
def font(px, weight=LABEL_WEIGHT, width=100):
    f = ImageFont.truetype(FONT, max(4, int(px)))
    try:
        f.set_variation_by_axes([weight, width])
    except Exception:
        pass
    return f


# ---------------------------------------------------------------- face
def base_face(D, SS):
    """Flat graphite with a soft radial falloff and a faint hairline at the rim."""
    S = D * SS
    g = Image.radial_gradient("L").resize((S, S), Image.BILINEAR)   # 0 centre -> 255 at r = S/2
    lut = []
    for v in range(256):
        t = min(1.0, v / 255.0) ** 1.7
        lut.append(int(255 * t))
    t = g.point(lut)
    face = Image.composite(Image.new("RGB", (S, S), BG_EDGE), Image.new("RGB", (S, S), BG_CENTER), t)
    # faint top sheen
    sheen = Image.new("L", (S, S), 0)
    ImageDraw.Draw(sheen).ellipse([S * 0.18, -S * 0.25, S * 0.82, S * 0.42], fill=60)
    sheen = sheen.filter(ImageFilter.GaussianBlur(S * 0.12))
    face = Image.composite(Image.new("RGB", (S, S), (40, 43, 48)), face, sheen)
    # rim hairline
    rim = Image.new("L", (S, S), 0)
    w = max(1, int(SS * 1.2))
    ImageDraw.Draw(rim).ellipse([SS * 3, SS * 3, S - SS * 3, S - SS * 3], outline=150, width=w)
    face = Image.composite(Image.new("RGB", (S, S), HAIR), face, rim)
    return face


# ---------------------------------------------------------------- canvas
class Canvas:
    """Layered drawing surface. Same API as the classic theme's Canvas, so the control-page
    generators work unchanged. Ink layers:
      seg = unlit track, hair = hairline, dim = secondary text, w = primary text,
      r = ACCENT (kept as 'r' so classic layouts map 1:1), x = WARN, k = text on filled buttons
    """
    LAYERS = (("seg", TRACK), ("hair", HAIR), ("dim", TEXT2), ("w", TEXT), ("r", ACCENT), ("x", WARN), ("k", ON_FILL))

    def __init__(self, D, SS=3):
        self.D, self.SS, self.S = D, SS, D * SS
        self.m = {k: Image.new("L", (self.S, self.S), 0) for k, _ in self.LAYERS}
        self.d = {k: ImageDraw.Draw(v) for k, v in self.m.items()}
        self.weight = LABEL_WEIGHT

    def s(self, v):
        return v * self.SS

    def add_layer(self, name, color, after="seg"):
        """Extra ink colour for this canvas only (e.g. a dim red track for a redline zone)."""
        self.m[name] = Image.new("L", (self.S, self.S), 0)
        self.d[name] = ImageDraw.Draw(self.m[name])
        order = list(self.LAYERS)
        i = [k for k, _ in order].index(after) + 1
        order.insert(i, (name, color))
        self.LAYERS = tuple(order)

    def base_face(self):
        return base_face(self.D, self.SS)

    def render(self):
        face = self.base_face()
        soft = lambda m: m.filter(ImageFilter.GaussianBlur(self.SS * 0.3))
        for k, col in self.LAYERS:
            face = Image.composite(Image.new("RGB", face.size, col), face, soft(self.m[k]))
        mask = Image.new("L", face.size, 0)
        ImageDraw.Draw(mask).ellipse([0, 0, self.S - 1, self.S - 1], fill=255)
        img = Image.composite(face, Image.new("RGB", face.size, (0, 0, 0)), mask)
        return img.resize((self.D, self.D), Image.LANCZOS)

    # ---- text ----
    def font(self, cap, weight=None):
        return font(int(self.s(cap) * 1.45), weight or self.weight)

    def text_mask(self, txt, cap, squeeze=1.0, weight=None):
        f = self.font(cap, weight)
        l, t, r, b = f.getbbox(txt)
        pad = int(self.s(6))
        m = Image.new("L", (r - l + 2 * pad, b - t + 2 * pad), 0)
        ImageDraw.Draw(m).text((pad - l, pad - t), txt, font=f, fill=255)
        m = m.crop(m.getbbox())
        hb = f.getbbox("H")
        k = self.s(cap) / (hb[3] - hb[1])
        return m.resize((max(1, int(m.width * k * squeeze)), max(1, int(m.height * k))), Image.LANCZOS)

    def text(self, ink, txt, cap, cx, y, anchor="c", max_w=None, squeeze=1.0, weight=None):
        if max_w:
            while cap > 5 and self.text_mask(txt, cap, squeeze, weight).width / self.SS > max_w:
                cap -= 0.5
        g = self.text_mask(txt, cap, squeeze, weight)
        f = self.font(cap, weight)
        hb, tb = f.getbbox("H"), f.getbbox(txt)
        k = self.s(cap) / (hb[3] - hb[1])
        X = self.s(cx) - {"l": 0, "c": g.width / 2, "r": g.width}[anchor]
        self.m[ink].paste(255, (int(X), int(self.s(y) + (tb[1] - hb[1]) * k)), g)
        return g.width / self.SS

    def text_center(self, ink, txt, cap, cx, cy, squeeze=1.0, weight=None):
        g = self.text_mask(txt, cap, squeeze, weight)
        self.m[ink].paste(255, (int(self.s(cx) - g.width / 2), int(self.s(cy) - g.height / 2)), g)

    # ---- shapes (screen px) ----
    def rrect(self, ink, x0, y0, x1, y1, r, w=None, fill=False):
        box = [self.s(x0), self.s(y0), self.s(x1), self.s(y1)]
        if fill:
            self.d[ink].rounded_rectangle(box, radius=self.s(r), fill=255)
        else:
            self.d[ink].rounded_rectangle(box, radius=self.s(r), outline=255, width=max(1, int(self.s(w))))

    def ring(self, ink, cx, cy, r, w):
        s = self.s
        self.d[ink].ellipse([s(cx - r), s(cy - r), s(cx + r), s(cy + r)], outline=255, width=max(1, int(s(w))))

    def disc(self, ink, cx, cy, r):
        s = self.s
        self.d[ink].ellipse([s(cx - r), s(cy - r), s(cx + r), s(cy + r)], fill=255)

    def line(self, ink, pts, w):
        self.d[ink].line([(self.s(x), self.s(y)) for x, y in pts], fill=255, width=max(1, int(self.s(w))), joint="curve")
        for x, y in (pts[0], pts[-1]):
            self.disc(ink, x, y, w / 2)

    def poly(self, ink, pts):
        self.d[ink].polygon([(self.s(x), self.s(y)) for x, y in pts], fill=255)

    def sector(self, ink, cx, cy, r_out, r_in, a0, a1, steps=90):
        def P(r, a):
            a = math.radians(a)
            return self.s(cx + r * math.sin(a)), self.s(cy - r * math.cos(a))
        pts = [P(r_out, a0 + (a1 - a0) * i / steps) for i in range(steps + 1)]
        pts += [P(r_in, a1 - (a1 - a0) * i / steps) for i in range(steps + 1)]
        self.d[ink].polygon(pts, fill=255)

    def arc_track(self, ink, cx, cy, r_mid, width, a0, a1, caps=True):
        """Arc band with rounded ends. Angles from 12 o'clock, clockwise."""
        self.sector(ink, cx, cy, r_mid + width / 2, r_mid - width / 2, a0, a1, steps=max(30, int(abs(a1 - a0) * 1.5)))
        if caps:
            for a in (a0, a1):
                x = cx + r_mid * math.sin(math.radians(a))
                y = cy - r_mid * math.cos(math.radians(a))
                self.disc(ink, x, y, width / 2)

    def tick(self, ink, cx, cy, a, r_out, r_in, w):
        x0, y0 = cx + r_out * math.sin(math.radians(a)), cy - r_out * math.cos(math.radians(a))
        x1, y1 = cx + r_in * math.sin(math.radians(a)), cy - r_in * math.cos(math.radians(a))
        self.line(ink, [(x0, y0), (x1, y1)], w)


def to_lvgl(deg_from_12):
    return round((deg_from_12 + 270) % 360, 2)
