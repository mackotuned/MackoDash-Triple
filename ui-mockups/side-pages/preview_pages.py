"""Approximate previews of the new side pages from the firmware coordinates."""
import math
import os
from PIL import Image, ImageDraw, ImageFont

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "side_pages_preview.png")
MICRO = r"C:\Users\mackb\AppData\Local\Microsoft\Windows\Fonts\Microgramma D Extended Bold.otf"
SANS = r"C:\Windows\Fonts\bahnschrift.ttf"
S = 2  # supersample

PALETTES = {
    "classic": dict(background=0x000000, panel=0x0C0C0D, border=0x2C2C2F, text=0xECECE7,
                    dim=0x9A9A96, accent=0xE81E1E, accent2=0xEC5C28, grid=0x19191B),
    "graphite": dict(background=0x0B0E11, panel=0x12171C, border=0x26323C, text=0xEEF0F2,
                     dim=0x8A97A3, accent=0x5EC8F2, accent2=0xE8EDF2, grid=0x18212A),
}


def rgb(v):
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


def font(path, px):
    return ImageFont.truetype(path, px * S)


def text(d, xy, s, f, color, anchor):
    d.text((xy[0] * S, xy[1] * S), s, font=f, fill=rgb(color), anchor=anchor)


def panel(d, p, x, y, w, h):
    d.rounded_rectangle([x * S, y * S, (x + w) * S, (y + h) * S], radius=12 * S,
                        fill=rgb(p["panel"]), outline=rgb(p["border"]), width=2 * S)


def arc(d, cx, cy, size, start, end, width, color):
    r = size / 2
    box = [(cx - r + width / 2) * S, (cy - r + width / 2) * S,
           (cx + r - width / 2) * S, (cy + r - width / 2) * S]
    d.arc(box, start, end, fill=rgb(color), width=width * S)


def base(p):
    img = Image.new("RGB", (480 * S, 480 * S), rgb(p["background"]))
    return img, ImageDraw.Draw(img)


def gauges(p):
    img, d = base(p)
    sm, mid = font(SANS, 14), font(MICRO, 35)
    text(d, (240, 78), "GAUGES", sm, p["accent"], "mt")
    data = [("COOLANT", "192", "F"), ("INTAKE", "88", "F"), ("AFR", "14.7", "AFR"),
            ("OIL PRESS", "52", "PSI"), ("BATTERY", "14.1", "VOLTS"), ("ETHANOL", "85", "%")]
    for i, (x, y) in enumerate([(84, 118), (246, 118), (84, 214), (246, 214), (84, 310), (246, 310)]):
        panel(d, p, x, y, 150, 88)
        n, v, u = data[i]
        text(d, (x + 75, y + 8), n, sm, p["dim"], "mt")
        text(d, (x + 75, y + 48), v, mid, p["text"], "mm")
        text(d, (x + 75, y + 82), u, sm, p["dim"], "mb")
    return img


def trends(p):
    img, d = base(p)
    sm, mid = font(SANS, 14), font(MICRO, 35)
    text(d, (240, 58), "TRENDS", sm, p["accent"], "mt")
    text(d, (240, 76), "LAST 20 SECONDS", sm, p["dim"], "mt")
    for cx, n, v, u, c in ((165, "BOOST", "12.4", "PSI", p["accent"]), (315, "AFR", "11.8", "AFR", p["accent2"])):
        text(d, (cx, 106), n, sm, c, "mt")
        text(d, (cx, 124), v, mid, p["text"], "mt")
        text(d, (cx, 162), u, sm, p["dim"], "mt")
    x0, y0, w, h = 70, 190, 340, 196
    panel(d, p, x0, y0, w, h)
    ix, iy, iw, ih = x0 + 12, y0 + 12, w - 24, h - 24
    for k in range(1, 5):
        yy = iy + ih * k / 5
        d.line([ix * S, yy * S, (ix + iw) * S, yy * S], fill=rgb(p["grid"]), width=S)
    for series, color in ((0, p["accent"]), (1, p["accent2"])):
        pts = []
        for i in range(100):
            t = i / 99
            if series == 0:
                val = 0.25 + 0.6 * max(0, math.sin(t * 6.5 - 1.2)) ** 1.5
            else:
                val = 0.55 - 0.25 * max(0, math.sin(t * 6.5 - 1.0)) + 0.03 * math.sin(t * 40)
            pts.append(((ix + iw * t) * S, (iy + ih * (1 - val)) * S))
        d.line(pts, fill=rgb(color), width=3 * S, joint="curve")
    return img


def peaks(p):
    img, d = base(p)
    sm, big, small = font(SANS, 14), font(MICRO, 35), font(MICRO, 28)
    text(d, (240, 58), "SESSION PEAKS", sm, p["accent"], "mt")
    cy = 240 - 52
    # LVGL arc angles: 0 = 3 o'clock, clockwise; PIL uses the same convention.
    arc(d, 240, cy, 196, 135, 405, 14, p["panel"])
    arc(d, 240, cy, 196, 135, 135 + 270 * 7900 / 9000, 14, p["accent"])
    arc(d, 240, cy, 160, 135, 135 + 270 * 4200 / 9000, 4, p["text"])
    text(d, (240, cy - 30), "PEAK RPM", sm, p["dim"], "mm")
    text(d, (240, cy + 4), "7900", big, p["text"], "mm")
    text(d, (240, cy + 36), "NOW 4200", sm, p["dim"], "mm")
    tiles = [("SPEED", "96"), ("BOOST", "17.2"), ("KNOCK", "2"), ("COOLANT", "201")]
    for (x, y), (n, v) in zip([(86, 292), (244, 292), (86, 352), (244, 352)], tiles):
        panel(d, p, x, y, 150, 54)
        text(d, (x + 12, y + 5), n, sm, p["dim"], "lt")
        text(d, (x + 138, y + 51), v, small, p["text"], "rb")
    text(d, (240, 414), "HOLD TO RESET", sm, p["dim"], "mt")
    return img


def timer(p):
    img, d = base(p)
    sm, huge, small = font(SANS, 14), font(MICRO, 72), font(MICRO, 28)
    text(d, (240, 58), "PERFORMANCE", sm, p["accent"], "mt")
    text(d, (240, 80), "RUN COMPLETE", sm, p["accent2"], "mt")
    text(d, (240, 104), "13.42", huge, p["text"], "mt")
    text(d, (240, 186), "0 MPH", sm, p["dim"], "mt")
    rows = [("0-30 MPH", "2.31"), ("0-60 MPH", "5.87"), ("1/8 MILE @ 81", "8.79"), ("1/4 MILE @ 104", "13.42")]
    for i, (n, v) in enumerate(rows):
        y = 214 + i * 44
        panel(d, p, 92, y, 296, 38)
        text(d, (106, y + 19), n, sm, p["dim"], "lm")
        text(d, (374, y + 20), v, small, p["text"], "rm")
    text(d, (240, 398), "HOLD TO RESET", sm, p["dim"], "mt")
    return img


def round_mask(img):
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).ellipse([0, 0, img.size[0] - 1, img.size[1] - 1], fill=255)
    out = Image.new("RGB", img.size, (40, 40, 44))
    out.paste(img, (0, 0), mask)
    return out.resize((480, 480), Image.LANCZOS)


pages = [gauges, trends, peaks, timer]
sheet = Image.new("RGB", (len(pages) * 490 + 10, 2 * 490 + 10), (40, 40, 44))
for row, name in enumerate(("classic", "graphite")):
    for col, page in enumerate(pages):
        sheet.paste(round_mask(page(PALETTES[name])), (10 + col * 490, 10 + row * 490))
sheet.save(OUT)
print(OUT)
