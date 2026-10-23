"""
Air ride pressure screen - 480x480 ROUND display, same face and lettering as the tach / side screen.

  top        : FRONT marker
  corners    : FL | FR  (upper)   RL | RR  (lower), psi
  centre     : TANK, big number, psi
  bottom     : REAR marker

Printed on the background: labels, units, markers. LVGL draws the five numbers.
Reuses the drawing helpers from make_side.py (keep both files in the same folder).

Outputs:
  air_480_round.png          - background for LVGL
  air_480_round_preview.png  - with sample values, for checking only
  air_layout.json            - value boxes
"""
import json
from PIL import Image, ImageDraw, ImageFilter
from make_side import (D, S, C, SS, s, base_face, text_mask, put_text, label_with_unit,
                       INK_WHITE, INK_ORANGE, INK_LABEL, NUM_SQUEEZE)

CORNER_DX = 94                 # corner value centres at 240 -/+ this
TOP_LABEL_Y = 112
BOT_LABEL_Y = 300
CORNERS = [("fl", "FL", -1, TOP_LABEL_Y), ("fr", "FR", +1, TOP_LABEL_Y),
           ("rl", "RL", -1, BOT_LABEL_Y), ("rr", "RR", +1, BOT_LABEL_Y)]


def chevron(d, cx, y, up=True, w=9, h=5):
    if up:
        pts = [(cx - w, y + h), (cx, y), (cx + w, y + h)]
    else:
        pts = [(cx - w, y), (cx, y + h), (cx + w, y)]
    d.line([(s(x), s(yy)) for x, yy in pts], fill=255, width=int(s(2)), joint="curve")


def build():
    face = base_face()
    white, orange, label = (Image.new("L", face.size, 0) for _ in range(3))
    dw, do, dl = (ImageDraw.Draw(m) for m in (white, orange, label))
    L = {"screen": [D, D], "round": True, "center": [240, 240], "items": {}}

    # orientation markers
    chevron(do, 240, 50, up=True)
    put_text(label, "FRONT", 8, 240, 62)
    put_text(label, "REAR", 8, 240, 410)
    chevron(do, 240, 424, up=False)

    # corners: label above value
    for key, lab, side, ly in CORNERS:
        cx = 240 + side * CORNER_DX
        label_with_unit(label, orange, lab, "PSI", 8.5, cx, ly)
        L["items"][key] = dict(value_box=dict(x=cx - 55, y=ly + 14, w=110, h=40), align="center",
                               cap_height=28, unit="PSI", range=[0, 150], format="%d")

    # thin dividers between left and right corners
    for ly in (TOP_LABEL_Y, BOT_LABEL_Y):
        dl.rectangle([C - SS * 0.4, s(ly + 2), C + SS * 0.4, s(ly + 50)], fill=255)

    # centre: tank
    label_with_unit(label, orange, "TANK", "PSI", 9.5, 240, 188)
    L["items"]["tank"] = dict(value_box=dict(x=140, y=204, w=200, h=62), align="center", cap_height=44,
                              unit="PSI", range=[0, 200], format="%d")
    do.rectangle([s(240 - 18), s(276), s(240 + 18), s(278.5)], fill=255)   # orange accent

    soft = lambda m: m.filter(ImageFilter.GaussianBlur(SS * 0.35))
    for m, col in ((label, INK_LABEL), (white, INK_WHITE), (orange, INK_ORANGE)):
        face = Image.composite(Image.new("RGB", face.size, col), face, soft(m))
    mask = Image.new("L", face.size, 0)
    ImageDraw.Draw(mask).ellipse([0, 0, S - 1, S - 1], fill=255)
    return Image.composite(face, Image.new("RGB", face.size, (0, 0, 0)), mask), L


def preview(big, L):
    img = big.copy()
    sample = dict(tank="165", fl="82", fr="84", rl="68", rr="70")
    w = Image.new("L", img.size, 0)
    for key, txt in sample.items():
        vb = L["items"][key]["value_box"]
        g = text_mask(txt, L["items"][key]["cap_height"], NUM_SQUEEZE * 1.1)
        w.paste(255, (int(s(vb["x"] + vb["w"] / 2) - g.width / 2), int(s(vb["y"] + vb["h"] / 2) - g.height / 2)), g)
    return Image.composite(Image.new("RGB", img.size, INK_WHITE), img, w)


if __name__ == "__main__":
    big, L = build()
    big.resize((D, D), Image.LANCZOS).save("air_480_round.png")
    preview(big, L).resize((D, D), Image.LANCZOS).save("air_480_round_preview.png")
    with open("air_layout.json", "w", encoding="utf-8") as f:
        json.dump(L, f, indent=2)
    print("ok")
