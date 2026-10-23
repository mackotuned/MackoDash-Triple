"""
Honda EK Civic SiR / Si (B16) tachometer face - 800x800 for a round ESP32 display.
Recreated from the OEM cluster: 0-9 x1000 r/min, red segmented band 8-9,
hatched red-zone wedge, textured black face. No bezel: the face runs edge to edge.

The needle and centre cap are NOT drawn on the main image; LVGL draws them on top.
Pure Pillow (no numpy). Rendered at 3x then downsampled.

Outputs:
  tach_ek_800.png          - background for LVGL (no needle)
  tach_ek_800_preview.png  - with a sample needle at 6500 rpm (for checking only)
"""
import math
import os
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops

OUT = 800
SS = 3
S = OUT * SS
C = S / 2

# ---------------- geometry (all radii in final 800px units) ----------------
RPM_MAX = 9.0
START_DEG = -130.0           # 0 rpm, degrees from 12 o'clock, clockwise +
SWEEP_DEG = 260.0            # 0 -> 9000 rpm
RED_START = 8.0
RED_END = 9.25               # orange band runs slightly past 9, like the OEM face

# Design units below are the original layout (face edge = 372). K scales the whole layout
# so the face edge lands on the display edge (400 px).
R_FACE = 372                 # printed face edge (design units)
R = 350                      # outer edge of the tick ring (design units)
K = (OUT / 2) / R_FACE       # design units -> final px

# ---------------- colours ----------------
FACE_RGB = (19, 19, 21)
INK_WHITE = (236, 236, 231)
INK_RED = (232, 30, 30)           # true red: redline band, hatch, red numerals

FONT_DIR = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "Windows", "Fonts")
FONT_NUM = os.path.join(FONT_DIR, "Microgramma D Extended Bold.otf")
if not os.path.exists(FONT_NUM):
    FONT_NUM = "C:/Windows/Fonts/bahnschrift.ttf"
NUM_SQUEEZE = 0.74           # Microgramma Extended is wider than Honda's numerals


def s(v):
    return v * SS * K


def ang(rpm):
    return START_DEG + SWEEP_DEG * rpm / RPM_MAX


def polar(r, deg):
    a = math.radians(deg)
    return C + s(r) * math.sin(a), C - s(r) * math.cos(a)


def circle(d, r, **kw):
    d.ellipse([C - s(r), C - s(r), C + s(r), C + s(r)], **kw)


def tick(d, deg, r_out, r_in, width, fill=255):
    a = math.radians(deg)
    nx, ny = math.cos(a) * s(width) / 2, math.sin(a) * s(width) / 2
    x1, y1 = polar(r_out, deg)
    x2, y2 = polar(r_in, deg)
    d.polygon([(x1 - nx, y1 - ny), (x1 + nx, y1 + ny), (x2 + nx, y2 + ny), (x2 - nx, y2 - ny)], fill=fill)


def sector(d, r_out, r_in, a0, a1, fill=255, steps=300):
    pts = [polar(r_out, a0 + (a1 - a0) * i / steps) for i in range(steps + 1)]
    pts += [polar(r_in, a1 - (a1 - a0) * i / steps) for i in range(steps + 1)]
    d.polygon(pts, fill=fill)


def text_mask(txt, cap_h, squeeze, italic=0.0):
    """Render text to an L mask with a given cap height (final px), squeezed horizontally."""
    f = ImageFont.truetype(FONT_NUM, int(s(cap_h) * 1.45))
    l, t, r_, b = f.getbbox(txt)
    pad = int(s(10))
    m = Image.new("L", (r_ - l + 2 * pad, b - t + 2 * pad), 0)
    ImageDraw.Draw(m).text((pad - l, pad - t), txt, font=f, fill=255)
    m = m.crop(m.getbbox())
    h = m.height
    scale = s(cap_h) / h
    m = m.resize((max(1, int(m.width * scale * squeeze)), int(h * scale)), Image.LANCZOS)
    if italic:
        sh = italic
        w2 = int(m.width + abs(sh) * m.height)
        m = m.transform((w2, m.height), Image.AFFINE, (1, sh, -sh * m.height if sh > 0 else 0, 0, 1, 0),
                        Image.BICUBIC)
    return m


def paste_center(mask, glyph, xy):
    mask.paste(255, (int(xy[0] - glyph.width / 2), int(xy[1] - glyph.height / 2)), glyph)


def radial(r_in, r_out, v_in, v_out, size=S):
    """L image: value v_in at radius<=r_in ramping to v_out at r_out (final px)."""
    g = Image.radial_gradient("L")  # 256x256, 0 centre -> 255 edge (at radius 128)
    g = g.resize((size, size), Image.BILINEAR)
    # g value v corresponds to radius v/255 * (S/2) * sqrt(2)? -> radial_gradient hits 255 at r=128 of 256
    lut = []
    for v in range(256):
        r = v / 255 * (S / 2) / SS / K  # radius in design units
        t = min(1, max(0, (r - r_in) / max(1e-6, r_out - r_in)))
        lut.append(int(v_in + (v_out - v_in) * t))
    return g.point(lut)


def build_face():
    # ---------- base face with woven texture ----------
    face = Image.new("RGB", (S, S), FACE_RGB)

    cell = int(round(s(4)))                                     # fine grid, ~4px at 800
    tile = Image.new("L", (cell * 2, cell * 2), 128)
    td = ImageDraw.Draw(tile)
    td.rectangle([0, 0, cell - 1, cell - 1], fill=146)
    td.rectangle([cell, cell, 2 * cell - 1, 2 * cell - 1], fill=146)
    td.rectangle([cell, 0, 2 * cell - 1, cell - 1], fill=112)
    td.rectangle([0, cell, cell - 1, 2 * cell - 1], fill=112)
    # thin dark grout lines between cells
    for k in (0, cell):
        td.line([(k, 0), (k, 2 * cell)], fill=96, width=max(1, SS // 2))
        td.line([(0, k), (2 * cell, k)], fill=96, width=max(1, SS // 2))
    tile = tile.filter(ImageFilter.GaussianBlur(SS * 0.45))
    tex = Image.new("L", (S, S))
    for y in range(0, S, tile.height):
        for x in range(0, S, tile.width):
            tex.paste(tile, (x, y))
    # map texture to +/- brightness around face colour
    tex_rgb = Image.merge("RGB", [tex.point(lambda v, c=c: max(0, min(255, c + (v - 128) * 0.24))) for c in FACE_RGB])
    face = tex_rgb

    # soft lighting: slightly brighter upper area, darker towards rim
    light = Image.new("L", (S, S), 0)
    ld = ImageDraw.Draw(light)
    ld.ellipse([C - s(300), C - s(390), C + s(300), C + s(130)], fill=255)
    light = light.filter(ImageFilter.GaussianBlur(s(110)))
    face = Image.composite(ImageChops.add(face, Image.new("RGB", (S, S), (9, 9, 10))), face, light)
    vign = radial(200, R_FACE, 0, 120)
    face = Image.composite(Image.new("RGB", (S, S), (4, 4, 5)), face, vign)

    # ---------- printed graphics ----------
    white = Image.new("L", (S, S), 0)
    orange = Image.new("L", (S, S), 0)
    orange_lt = Image.new("L", (S, S), 0)
    dw, do, dl = ImageDraw.Draw(white), ImageDraw.Draw(orange), ImageDraw.Draw(orange_lt)

    # ticks every 250 rpm
    n = int(RPM_MAX * 4)
    for i in range(n + 1):
        rpm = i / 4
        if rpm >= RED_START:
            continue
        if i % 4 == 0:
            tick(dw, ang(rpm), R, R - 34, 9.5)
        else:
            tick(dw, ang(rpm), R, R - 22, 5.0)

    # orange segmented band 8 -> 9.25 with dark separators every 250 rpm
    sector(do, R, R - 22, ang(RED_START), ang(RED_END))
    for i in range(int(RED_START * 4), int(RED_END * 4) + 1):
        rpm = i / 4
        if rpm in (RED_START, RED_END):
            continue
        tick(do, ang(rpm), R + 2, R - 24, 2.6, fill=0)
    tick(do, ang(RED_START), R, R - 34, 9.5)             # the 8 major tick in orange
    tick(dw, ang(RED_END) + 0.9, R + 1, R - 23, 5.0)     # light end cap, as on the OEM face

    # hatched red-zone wedge
    hatch_area = Image.new("L", (S, S), 0)
    sector(ImageDraw.Draw(hatch_area), 246, 178, ang(7.78), ang(9.36))
    stripes = Image.new("L", (S, S), 0)
    sd = ImageDraw.Draw(stripes)
    pitch = s(22)
    slope = math.tan(math.radians(-17))                   # stripes rise to the right
    y = -S
    while y < 2 * S:
        sd.polygon([(0, y), (S, y + slope * S), (S, y + slope * S + pitch * 0.58), (0, y + pitch * 0.58)], fill=255)
        y += pitch
    hatch = ImageChops.multiply(hatch_area, stripes)
    orange_lt.paste(255, (0, 0), hatch)

    # numerals
    for k in range(int(RPM_MAX) + 1):
        g = text_mask(str(k), 38, NUM_SQUEEZE)
        xy = polar(R - 64, ang(k))
        paste_center(orange_lt if k >= RED_START else white, g, xy)

    # legend
    g = text_mask("x1000r/min", 24, 0.95)
    paste_center(white, g, (C, C - s(128)))

    # slight ink softness, like screen print under glass
    soft = lambda m: m.filter(ImageFilter.GaussianBlur(SS * 0.35))
    face = Image.composite(Image.new("RGB", (S, S), INK_WHITE), face, soft(white))
    face = Image.composite(Image.new("RGB", (S, S), INK_RED), face, soft(orange))
    face = Image.composite(Image.new("RGB", (S, S), INK_RED), face, soft(orange_lt))

    # two small pin holes either side of the hub (present on the OEM face)
    holes = Image.new("L", (S, S), 0)
    hd = ImageDraw.Draw(holes)
    for dx in (-148, 158):
        hd.ellipse([C + s(dx) - s(9), C + s(4) - s(9), C + s(dx) + s(9), C + s(4) + s(9)], fill=255)
    face = Image.composite(Image.new("RGB", (S, S), (3, 3, 3)), face, holes.filter(ImageFilter.GaussianBlur(SS)))
    hl = Image.new("L", (S, S), 0)
    hld = ImageDraw.Draw(hl)
    for dx in (-148, 158):
        hld.ellipse([C + s(dx + 2), C + s(4 - 5), C + s(dx + 5), C + s(4 - 2)], fill=200)
    face = Image.composite(Image.new("RGB", (S, S), (190, 190, 190)), face, hl.filter(ImageFilter.GaussianBlur(SS * 0.8)))

    out = Image.new("RGB", (S, S), (0, 0, 0))
    m_face = Image.new("L", (S, S), 0)
    circle(ImageDraw.Draw(m_face), R_FACE, fill=255)
    out = Image.composite(face, out, m_face)
    return out


def add_preview_needle(img, rpm=6.5):
    S2 = img.width
    k = S2 / OUT * K
    layer = Image.new("L", img.size, 0)
    d = ImageDraw.Draw(layer)
    deg = ang(rpm)
    a = math.radians(deg)
    c2 = S2 / 2

    def p(r):
        return c2 + r * k * math.sin(a), c2 - r * k * math.cos(a)
    nx, ny = math.cos(a), math.sin(a)
    tip, tail = p(338), p(-40)
    w0, w1 = 7 * k, 3.5 * k
    d.polygon([(tail[0] - nx * w0, tail[1] - ny * w0), (tail[0] + nx * w0, tail[1] + ny * w0),
               (tip[0] + nx * w1, tip[1] + ny * w1), (tip[0] - nx * w1, tip[1] - ny * w1)], fill=255)
    shadow = layer.filter(ImageFilter.GaussianBlur(5 * k))
    img = Image.composite(Image.new("RGB", img.size, (0, 0, 0)), img,
                          ImageChops.offset(shadow, int(4 * k), int(6 * k)).point(lambda v: v * 0.6))
    img = Image.composite(Image.new("RGB", img.size, (236, 92, 40)), img, layer)
    cap = Image.new("L", img.size, 0)
    r = 46 * k
    ImageDraw.Draw(cap).ellipse([c2 - r, c2 - r, c2 + r, c2 + r], fill=255)
    img = Image.composite(Image.new("RGB", img.size, (14, 14, 15)), img, cap)
    return img


if __name__ == "__main__":
    big = build_face()
    big.resize((OUT, OUT), Image.LANCZOS).save("tach_ek_800.png")
    add_preview_needle(big).resize((OUT, OUT), Image.LANCZOS).save("tach_ek_800_preview.png")
    print("0 rpm @", ang(0), "deg   9000 @", ang(9), "deg   (from 12 o'clock, clockwise)")
