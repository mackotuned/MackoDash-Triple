"""
EK Civic SiR-style speedometer face - 480x480 ROUND display, matching the tach:
same textured face, tick style, numerals, white ink and true red.

  main scale  : 0-160 mph, white, numerals every 20, same 260 deg sweep as the tach
  inner scale : km/h in red (0-240, numerals every 40), like the OEM dual-unit speedo

The needle and centre cap are NOT drawn on the main image; LVGL draws them on top.
Layout is in the tach's design units (face edge = 372) and scaled by K, so proportions match the tach.

Outputs:
  speedo_480_round.png          - background for LVGL
  speedo_480_round_preview.png  - with a sample needle at 68 mph, for checking only
"""
import math
import os
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops

OUT = 480
SS = 4
S = OUT * SS
C = S / 2

# ---------------- geometry (design units, same as the tach) ----------------
MPH_MAX = 160
START_DEG, SWEEP_DEG = -130.0, 260.0
R_FACE = 372
R = 350                         # outer edge of tick ring
K = (OUT / 2) / R_FACE          # design units -> final px

SHOW_KMH = True                 # inner red km/h scale
KMH_MAX_LABEL = 240

# ---------------- colours ----------------
FACE_RGB = (19, 19, 21)
INK_WHITE = (236, 236, 231)
INK_RED = (232, 30, 30)

FONT_DIR = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "Windows", "Fonts")
FONT = os.path.join(FONT_DIR, "Microgramma D Extended Bold.otf")
if not os.path.exists(FONT):
    FONT = "C:/Windows/Fonts/bahnschrift.ttf"
NUM_SQUEEZE = 0.74


def s(v):
    return v * SS * K


def ang_mph(v):
    return START_DEG + SWEEP_DEG * v / MPH_MAX


def ang_kmh(v):
    return ang_mph(v / 1.609344)


def polar(r, deg):
    a = math.radians(deg)
    return C + s(r) * math.sin(a), C - s(r) * math.cos(a)


def tick(d, deg, r_out, r_in, width, fill=255):
    a = math.radians(deg)
    nx, ny = math.cos(a) * s(width) / 2, math.sin(a) * s(width) / 2
    x1, y1 = polar(r_out, deg)
    x2, y2 = polar(r_in, deg)
    d.polygon([(x1 - nx, y1 - ny), (x1 + nx, y1 + ny), (x2 + nx, y2 + ny), (x2 - nx, y2 - ny)], fill=fill)


def sector(d, r_out, r_in, a0, a1, fill=255, steps=200):
    pts = [polar(r_out, a0 + (a1 - a0) * i / steps) for i in range(steps + 1)]
    pts += [polar(r_in, a1 - (a1 - a0) * i / steps) for i in range(steps + 1)]
    d.polygon(pts, fill=fill)


def text_mask(txt, cap_h, squeeze=1.0):
    """cap_h in design units."""
    f = ImageFont.truetype(FONT, int(s(cap_h) * 1.45))
    l, t, r, b = f.getbbox(txt)
    pad = int(s(10))
    m = Image.new("L", (r - l + 2 * pad, b - t + 2 * pad), 0)
    ImageDraw.Draw(m).text((pad - l, pad - t), txt, font=f, fill=255)
    m = m.crop(m.getbbox())
    hb = f.getbbox("H")
    k = s(cap_h) / (hb[3] - hb[1])
    return m.resize((max(1, int(m.width * k * squeeze)), max(1, int(m.height * k))), Image.LANCZOS)


def paste_center(mask, g, xy):
    mask.paste(255, (int(xy[0] - g.width / 2), int(xy[1] - g.height / 2)), g)


def numeral_at(mask, txt, cap, deg, r_tick_in, gap, squeeze=NUM_SQUEEZE):
    """Place a numeral so its nearest edge sits `gap` inside r_tick_in, whatever its width."""
    g = text_mask(txt, cap, squeeze)
    a = math.radians(deg)
    w, h = g.width / (SS * K), g.height / (SS * K)
    ext = abs(w / 2 * math.sin(a)) + abs(h / 2 * math.cos(a))
    paste_center(mask, g, polar(r_tick_in - gap - ext, deg))


def base_face():
    """Same woven texture and lighting as the tach (texture cell kept at 4 final px, like the other 480 screens)."""
    cell = 4 * SS
    tile = Image.new("L", (cell * 2, cell * 2), 128)
    td = ImageDraw.Draw(tile)
    td.rectangle([0, 0, cell - 1, cell - 1], fill=146)
    td.rectangle([cell, cell, 2 * cell - 1, 2 * cell - 1], fill=146)
    td.rectangle([cell, 0, 2 * cell - 1, cell - 1], fill=112)
    td.rectangle([0, cell, cell - 1, 2 * cell - 1], fill=112)
    for k in (0, cell):
        td.line([(k, 0), (k, 2 * cell)], fill=96, width=max(1, SS // 2))
        td.line([(0, k), (2 * cell, k)], fill=96, width=max(1, SS // 2))
    tile = tile.filter(ImageFilter.GaussianBlur(SS * 0.45))
    tex = Image.new("L", (S, S))
    for y in range(0, S, tile.height):
        for x in range(0, S, tile.width):
            tex.paste(tile, (x, y))
    face = Image.merge("RGB", [tex.point(lambda v, c=c: max(0, min(255, c + (v - 128) * 0.24))) for c in FACE_RGB])
    light = Image.new("L", (S, S), 0)
    ImageDraw.Draw(light).ellipse([C - s(300), C - s(390), C + s(300), C + s(130)], fill=255)
    light = light.filter(ImageFilter.GaussianBlur(s(110)))
    face = Image.composite(ImageChops.add(face, Image.new("RGB", (S, S), (9, 9, 10))), face, light)
    # tach vignette: none inside r=200, ramping to 120/255 at the face edge
    g = Image.radial_gradient("L").resize((S, S), Image.BILINEAR)
    lut = []
    for v in range(256):
        r = v / 255 * (S / 2) / (SS * K)
        t = min(1, max(0, (r - 200) / (R_FACE - 200)))
        lut.append(int(120 * t))
    face = Image.composite(Image.new("RGB", (S, S), (4, 4, 5)), face, g.point(lut))
    return face


def build():
    face = base_face()
    white = Image.new("L", (S, S), 0)
    red = Image.new("L", (S, S), 0)
    dw, dr = ImageDraw.Draw(white), ImageDraw.Draw(red)

    # ---- main mph scale: tach-style ticks ----
    for v in range(0, MPH_MAX + 1, 5):
        if v % 20 == 0:
            tick(dw, ang_mph(v), R, R - 34, 9.5)          # same as tach majors
        elif v % 10 == 0:
            tick(dw, ang_mph(v), R, R - 22, 5.0)          # same as tach minors
        else:
            tick(dw, ang_mph(v), R, R - 14, 3.2)
    for v in range(0, MPH_MAX + 1, 20):
        numeral_at(white, str(v), 34, ang_mph(v), R - 34, 12)

    # ---- inner km/h scale in red ----
    if SHOW_KMH:
        r_k = 206
        for v in range(0, KMH_MAX_LABEL + 1, 20):
            tick(dr, ang_kmh(v), r_k, r_k - (12 if v % 40 == 0 else 7), 4.0 if v % 40 == 0 else 2.6)
        for v in range(0, KMH_MAX_LABEL + 1, 40):
            numeral_at(red, str(v), 17, ang_kmh(v), r_k - 12, 8)

    # ---- legends ----
    paste_center(white, text_mask("mph", 26, 0.95), (C, C + s(118)))
    if SHOW_KMH:
        paste_center(red, text_mask("km/h", 17, 0.95), (C, C + s(154)))

    soft = lambda m: m.filter(ImageFilter.GaussianBlur(SS * 0.35))
    face = Image.composite(Image.new("RGB", (S, S), INK_WHITE), face, soft(white))
    face = Image.composite(Image.new("RGB", (S, S), INK_RED), face, soft(red))

    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, S - 1, S - 1], fill=255)
    out = Image.composite(face, Image.new("RGB", (S, S), (0, 0, 0)), mask)

    layout = dict(
        needle=dict(center=[240, 240], length=round(338 * K), hub_radius=round(46 * K), range=[0, MPH_MAX],
                    lvgl_rotation=(START_DEG + 270) % 360, lvgl_sweep=SWEEP_DEG))
    return out, layout


def preview(img, mph=68):
    layer = Image.new("L", img.size, 0)
    d = ImageDraw.Draw(layer)
    deg = ang_mph(mph)
    a = math.radians(deg)
    nx, ny = math.cos(a), math.sin(a)
    tip, tail = polar(338, deg), polar(-40, deg)
    w0, w1 = s(7), s(3.5)
    d.polygon([(tail[0] - nx * w0, tail[1] - ny * w0), (tail[0] + nx * w0, tail[1] + ny * w0),
               (tip[0] + nx * w1, tip[1] + ny * w1), (tip[0] - nx * w1, tip[1] - ny * w1)], fill=255)
    sh = ImageChops.offset(layer.filter(ImageFilter.GaussianBlur(s(5))), int(s(4)), int(s(6))).point(lambda v: v * 0.6)
    img = Image.composite(Image.new("RGB", img.size, (0, 0, 0)), img, sh)
    img = Image.composite(Image.new("RGB", img.size, (236, 92, 40)), img, layer)
    cap = Image.new("L", img.size, 0)
    ImageDraw.Draw(cap).ellipse([C - s(46), C - s(46), C + s(46), C + s(46)], fill=255)
    return Image.composite(Image.new("RGB", img.size, (14, 14, 15)), img, cap)


if __name__ == "__main__":
    import json
    big, layout = build()
    big.resize((OUT, OUT), Image.LANCZOS).save("speedo_480_round.png")
    preview(big).resize((OUT, OUT), Image.LANCZOS).save("speedo_480_round_preview.png")
    with open("speedo_layout.json", "w") as f:
        json.dump(layout, f, indent=2)
    print(json.dumps(layout))
