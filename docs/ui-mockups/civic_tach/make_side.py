"""
Simple companion screen for the EK Civic SiR tach - 480x480 ROUND display.
Same textured face, lettering and white/orange ink as the tach.

  left arc   : FUEL, 8 segments, E (bottom) -> F (top), segmented like the tach's red band
  right arc  : WATER TEMP, 10 segments, C (bottom) -> H (top), top 2 = warning (orange)
               non-linear: 60F at the bottom, 190F (normal running) at the middle tick, 225F full
  centre     : BOOST, big number
  lower pair : AIR TEMP | BATTERY

Printed on the background: labels, units, icons, ticks, unlit segments.
LVGL draws: the three numbers and the lit segments.

Outputs:
  side_480_round.png          - background for LVGL
  side_480_round_preview.png  - with sample values, for checking only
  side_layout.json            - value boxes + segment angles (LVGL degrees)
"""
import json
import math
import os
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops

D = 480
SS = 3
S = D * SS
C = S / 2

FACE_RGB = (19, 19, 21)
INK_WHITE = (236, 236, 231)
INK_ORANGE = (240, 124, 78)
INK_LABEL = (182, 182, 176)
SEG_OFF = (44, 44, 47)

FONT_DIR = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "Windows", "Fonts")
FONT = os.path.join(FONT_DIR, "Microgramma D Extended Bold.otf")
if not os.path.exists(FONT):
    FONT = "C:/Windows/Fonts/bahnschrift.ttf"
NUM_SQUEEZE = 0.74            # same numeral proportions as the tach
DEG = "\u00b0"

# arcs, degrees from 12 o'clock clockwise; right arc spans ARC_SPAN, left arc mirrors it
ARC_R_OUT, ARC_R_IN = 226, 210
ARC_SPAN = (38, 142)
SEG_GAP = 2.2


def s(v):
    return v * SS


def polar(r, deg):
    a = math.radians(deg)
    return C + s(r) * math.sin(a), C - s(r) * math.cos(a)


def to_lvgl(d12):
    return (d12 + 270) % 360


def from_lvgl(a):
    v = (a - 270) % 360
    return v - 360 if v > 180 else v


def tick(d, deg, r_out, r_in, width, fill=255):
    a = math.radians(deg)
    nx, ny = math.cos(a) * s(width) / 2, math.sin(a) * s(width) / 2
    x1, y1 = polar(r_out, deg)
    x2, y2 = polar(r_in, deg)
    d.polygon([(x1 - nx, y1 - ny), (x1 + nx, y1 + ny), (x2 + nx, y2 + ny), (x2 - nx, y2 - ny)], fill=fill)


def sector(d, r_out, r_in, a0, a1, fill=255, steps=80):
    pts = [polar(r_out, a0 + (a1 - a0) * i / steps) for i in range(steps + 1)]
    pts += [polar(r_in, a1 - (a1 - a0) * i / steps) for i in range(steps + 1)]
    d.polygon(pts, fill=fill)


def font_for(cap_h):
    return ImageFont.truetype(FONT, int(s(cap_h) * 1.45))


def text_mask(txt, cap_h, squeeze=1.0):
    f = font_for(cap_h)
    l, t, r, b = f.getbbox(txt)
    pad = s(10)
    m = Image.new("L", (r - l + 2 * pad, b - t + 2 * pad), 0)
    ImageDraw.Draw(m).text((pad - l, pad - t), txt, font=f, fill=255)
    m = m.crop(m.getbbox())
    hb = f.getbbox("H")
    k = s(cap_h) / (hb[3] - hb[1])
    return m.resize((max(1, int(m.width * k * squeeze)), max(1, int(m.height * k))), Image.LANCZOS)


def text_w(txt, cap_h):
    return text_mask(txt, cap_h).width / SS


def put_text(mask, txt, cap_h, x, y, anchor="c"):
    """x,y screen px; y = top of capitals."""
    g = text_mask(txt, cap_h)
    f = font_for(cap_h)
    hb, tb = f.getbbox("H"), f.getbbox(txt)
    k = s(cap_h) / (hb[3] - hb[1])
    X = s(x) - {"l": 0, "c": g.width / 2, "r": g.width}[anchor]
    mask.paste(255, (int(X), int(s(y) + (tb[1] - hb[1]) * k)), g)


def put_centered(mask, txt, cap_h, cx, cy):
    g = text_mask(txt, cap_h)
    mask.paste(255, (int(s(cx) - g.width / 2), int(s(cy) - g.height / 2)), g)


def unit_w(unit, cap_h):
    deg = unit.startswith(DEG)
    txt = unit[1:] if deg else unit
    return text_w(txt, cap_h) + ((cap_h * 0.48 + 2.5) if deg else 0)


def put_unit(mask, unit, cap_h, x, y):
    """Left-aligned unit; the degree ring is drawn by hand (the font's degree sign is unusable)."""
    deg = unit.startswith(DEG)
    txt = unit[1:] if deg else unit
    if deg:
        r = cap_h * 0.24
        ImageDraw.Draw(mask).ellipse([s(x), s(y), s(x + 2 * r), s(y + 2 * r)], outline=255,
                                     width=max(1, int(s(cap_h * 0.15))))
        x += 2 * r + 2.5
    put_text(mask, txt, cap_h, x, y, anchor="l")


def label_with_unit(m_label, m_unit, label, unit, cap_h, cx, y):
    gap = cap_h * 0.7
    total = text_w(label, cap_h) + gap + unit_w(unit, cap_h)
    x0 = cx - total / 2
    put_text(m_label, label, cap_h, x0, y, anchor="l")
    put_unit(m_unit, unit, cap_h, x0 + text_w(label, cap_h) + gap, y)


def arc_segments(side, n):
    a_lo, a_hi = ARC_SPAN
    seg = (a_hi - a_lo - SEG_GAP * (n - 1)) / n
    out = []
    for i in range(n):                      # segment 0 at the bottom
        b = a_hi - i * (seg + SEG_GAP)
        a = b - seg
        out.append((a, b) if side > 0 else (-b, -a))
    return out


def fuel_icon(d, cx, cy):
    x, y = cx - 8, cy - 11
    d.rounded_rectangle([s(x), s(y), s(x + 12), s(y + 21)], radius=s(2), fill=255)
    d.rectangle([s(x + 2.5), s(y + 3), s(x + 9.5), s(y + 9)], fill=0)
    d.rectangle([s(x - 2), s(y + 19), s(x + 14), s(y + 22)], fill=255)
    w = int(s(2))
    d.line([s(x + 12), s(y + 6), s(x + 16), s(y + 9)], fill=255, width=w)
    d.line([s(x + 16), s(y + 9), s(x + 16), s(y + 17)], fill=255, width=w)
    d.line([s(x + 16), s(y + 17), s(x + 13.5), s(y + 17)], fill=255, width=w)


def temp_icon(d, cx, cy):
    cy -= 4
    d.line([s(cx), s(cy - 12), s(cx), s(cy + 4)], fill=255, width=int(s(4)))
    d.ellipse([s(cx - 5), s(cy + 2), s(cx + 5), s(cy + 12)], fill=255)
    for dy in (-9, -4):
        d.line([s(cx + 2), s(cy + dy), s(cx + 7), s(cy + dy)], fill=255, width=int(s(1.5)))
    pts = [(s(cx - 10 + i), s(cy + 16 + 1.8 * math.sin(i / 2.2))) for i in range(21)]
    d.line(pts, fill=255, width=int(s(2.2)))


def base_face():
    """Same woven-texture face and lighting as the tach."""
    cell = s(4)
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
    light = Image.new("L", face.size, 0)
    ImageDraw.Draw(light).ellipse([C - s(180), C - s(235), C + s(180), C + s(80)], fill=255)
    light = light.filter(ImageFilter.GaussianBlur(s(66)))
    face = Image.composite(ImageChops.add(face, Image.new("RGB", face.size, (9, 9, 10))), face, light)
    rim = Image.new("L", face.size, 255)
    ImageDraw.Draw(rim).ellipse([C - s(120), C - s(120), C + s(120), C + s(120)], fill=0)
    rim = rim.filter(ImageFilter.GaussianBlur(s(66))).point(lambda v: int(v * 0.5))
    return Image.composite(Image.new("RGB", face.size, (4, 4, 5)), face, rim)


def build():
    face = base_face()
    white, orange, label, segoff = (Image.new("L", face.size, 0) for _ in range(4))
    dw, do, dl, dso = (ImageDraw.Draw(m) for m in (white, orange, label, segoff))
    L = {"screen": [D, D], "round": True, "center": [240, 240],
         "angle_note": "LVGL degrees: 0 = 3 o'clock, clockwise", "items": {}}

    def arc_gauge(key, side, n, warn_from, lo, hi, lo_or, hi_or, icon):
        segs = arc_segments(side, n)
        for i, (a, b) in enumerate(segs):
            sector(dso, ARC_R_OUT, ARC_R_IN, a, b)
            if warn_from is not None and i >= warn_from:
                sector(do, ARC_R_IN - 4, ARC_R_IN - 6.5, a, b)
        a_lo, a_hi = ARC_SPAN
        # tach-style ticks inside the arc at both ends and the middle
        for deg, w in ((a_hi, 5), (90, 3.4), (a_lo, 5)):
            tick(dw, side * deg, ARC_R_IN - 4, ARC_R_IN - 14, w)
        for txt, deg, is_or in ((lo, a_hi - 7, lo_or), (hi, a_lo + 7, hi_or)):
            x, y = polar(ARC_R_IN - 23, side * deg)
            put_centered(orange if is_or else white, txt, 11, x / SS, y / SS)
        ix, iy = polar(ARC_R_IN - 34, side * 90)
        icon(dw, ix / SS, iy / SS)
        L["items"][key + "_arc"] = dict(
            radius_outer=ARC_R_OUT, width=ARC_R_OUT - ARC_R_IN, segment0="bottom",
            warn_from_segment=warn_from,
            segments=[dict(start=round(to_lvgl(a), 2), end=round(to_lvgl(b), 2)) for a, b in segs])
        return segs

    fsegs = arc_gauge("fuel", -1, 8, None, "E", "F", True, False, fuel_icon)
    sector(do, ARC_R_IN - 4, ARC_R_IN - 6.5, *fsegs[0])            # reserve marker
    L["items"]["fuel_arc"]["reserve_segment"] = 0
    arc_gauge("water", +1, 10, 8, "C", "H", False, True, temp_icon)

    L["items"]["water_arc"]["mapping"] = dict(type="piecewise", start_f=60, mid_f=190, max_f=225,
                                              note="60->0 segs, 190->5 segs (middle tick), 225->10 segs")

    # centre: boost
    label_with_unit(label, orange, "BOOST", "PSI", 9.5, 240, 150)
    L["items"]["boost"] = dict(value_box=dict(x=140, y=166, w=200, h=70), align="center", cap_height=50,
                               unit="PSI", range=[-15, 20], format="%.1f")
    do.rectangle([s(240 - 18), s(250), s(240 + 18), s(252.5)], fill=255)  # orange accent, like the tach band

    # lower pair: air temp | battery
    for key, lab, unit, cx, rng, fmt in (("iat", "AIR", DEG + "F", 240 - 62, [0, 200], "%d"),
                                         ("battery", "BATTERY", "V", 240 + 62, [10, 16], "%.1f")):
        label_with_unit(label, orange, lab, unit, 8, cx, 282)
        L["items"][key] = dict(value_box=dict(x=cx - 52, y=295, w=104, h=36), align="center", cap_height=24,
                               unit=unit, range=rng, format=fmt)
    dl.rectangle([C - SS * 0.4, s(284), C + SS * 0.4, s(328)], fill=255)  # divider

    soft = lambda m: m.filter(ImageFilter.GaussianBlur(SS * 0.35))
    for m, col in ((segoff, SEG_OFF), (label, INK_LABEL), (white, INK_WHITE), (orange, INK_ORANGE)):
        face = Image.composite(Image.new("RGB", face.size, col), face, soft(m))
    mask = Image.new("L", face.size, 0)
    ImageDraw.Draw(mask).ellipse([0, 0, S - 1, S - 1], fill=255)
    return Image.composite(face, Image.new("RGB", face.size, (0, 0, 0)), mask), L


def preview(big, L):
    img = big.copy()
    sample = dict(boost="11.2", iat="96", battery="14.1")
    lit = dict(fuel_arc=5, water_arc=5)
    w, o = Image.new("L", img.size, 0), Image.new("L", img.size, 0)
    dw, do = ImageDraw.Draw(w), ImageDraw.Draw(o)
    for key, txt in sample.items():
        it = L["items"][key]
        vb = it["value_box"]
        g = text_mask(txt, it["cap_height"], NUM_SQUEEZE * 1.1)
        w.paste(255, (int(s(vb["x"] + vb["w"] / 2) - g.width / 2), int(s(vb["y"] + vb["h"] / 2) - g.height / 2)), g)
    for key, n in lit.items():
        it = L["items"][key]
        warn = it.get("warn_from_segment")
        for k in range(n):
            sg = it["segments"][k]
            sector(do if (warn is not None and k >= warn) else dw, ARC_R_OUT, ARC_R_IN,
                   from_lvgl(sg["start"]), from_lvgl(sg["end"]))
    img = Image.composite(Image.new("RGB", img.size, INK_WHITE), img, w)
    return Image.composite(Image.new("RGB", img.size, INK_ORANGE), img, o)


if __name__ == "__main__":
    big, L = build()
    big.resize((D, D), Image.LANCZOS).save("side_480_round.png")
    preview(big, L).resize((D, D), Image.LANCZOS).save("side_480_round_preview.png")
    with open("side_layout.json", "w", encoding="utf-8") as f:
        json.dump(L, f, indent=2, ensure_ascii=False)
    print("ok")
