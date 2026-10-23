"""
GRAPHITE theme - the four gauge/display screens, arc style (no needles).

  tach_ek_800.png        800  rpm arc 0-9000, redline zone, big digital rpm
  speedo_480_round.png   480  speed arc 0-160 mph, big digital speed
  side_480_round.png     480  boost (centre), air temp + battery, fuel arc (left), water arc (right, non-linear)
  air_480_round.png      480  tank (centre) + four corner arcs

LVGL draws the arc fill (lv_arc indicator, rounded) and the numbers; tracks, scales, labels are printed.
Run from the theme folder:  python src/make_gauges.py
"""
import json
import math
from PIL import Image, ImageDraw
from theme import Canvas, ACCENT, WARN, TEXT, NUMBER_WEIGHT, to_lvgl, HEX

RED_TRACK = (74, 34, 34)          # unlit track inside a warning zone


def fill_preview(cv, cx, cy, r_mid, width, a_start, a_end, ink="r"):
    if abs(a_end - a_start) > 0.5:
        cv.arc_track(ink, cx, cy, r_mid, width, min(a_start, a_end), max(a_start, a_end))


def number(cv, txt, box, cap, ink="w"):
    x, y, w, h = box
    cv.text_center(ink, txt, cap, x + w / 2, y + h / 2, weight=NUMBER_WEIGHT)


def arc_spec(cx, cy, r_mid, width, a0, a1, reverse=False, rng=(0, 100)):
    return dict(center=[cx, cy], radius_mid=r_mid, width=width, obj_size=round(2 * (r_mid + width / 2)),
                bg_start=to_lvgl(a0), bg_end=to_lvgl(a1), mode="REVERSE" if reverse else "NORMAL", range=list(rng))


# ------------------------------------------------------------------ tach 800
def tach(preview_rpm=None):
    cv = Canvas(800, SS=3)
    cv.add_layer("xt", RED_TRACK)
    C = 400
    R_MID, W = 338, 26
    A0, A1 = -135.0, 135.0
    ang = lambda rpm: A0 + (A1 - A0) * rpm / 9000.0
    cv.arc_track("seg", C, C, R_MID, W, A0, ang(8000), caps=True)
    cv.arc_track("xt", C, C, R_MID, W, ang(8000), A1, caps=True)
    cv.sector("seg", C, C, R_MID + W / 2, R_MID - W / 2, ang(7900), ang(8000))     # seamless join
    for i in range(0, 19):
        rpm = i * 500
        major = i % 2 == 0
        ink = ("x" if rpm >= 8000 else "dim") if major else "hair"
        cv.tick(ink, C, C, ang(rpm), 318, 302 if major else 310, 3.2 if major else 2.2)
    for k in range(10):
        a = math.radians(ang(k * 1000))
        cv.text_center("x" if k >= 8 else "dim", str(k), 26, C + 272 * math.sin(a), C - 272 * math.cos(a), weight=500)
    cv.text("dim", "RPM", 15, C, 478)
    cv.text("dim", "x1000", 12, C, 250)
    vbox = (230, 326, 340, 128)
    if preview_rpm is not None:
        fill_preview(cv, C, C, R_MID, W, A0, ang(preview_rpm), "x" if preview_rpm >= 8000 else "r")
        number(cv, "%d" % preview_rpm, vbox, 104)
    layout = dict(size=[800, 800], arc=arc_spec(C, C, R_MID, W, A0, A1, rng=(0, 9000)),
                  redline_from=8000, value_box=dict(zip("xywh", vbox)), value_digit_px=104,
                  indicator_color=HEX(ACCENT), redline_color=HEX(WARN))
    return cv.render(), layout


# ------------------------------------------------------------------ speedo 480
def speedo(preview_mph=None):
    cv = Canvas(480, SS=4)
    C = 240
    R_MID, W = 204, 16
    A0, A1 = -135.0, 135.0
    ang = lambda v: A0 + (A1 - A0) * v / 160.0
    cv.arc_track("seg", C, C, R_MID, W, A0, A1)
    for v in range(0, 161, 10):
        major = v % 20 == 0
        cv.tick("dim" if major else "hair", C, C, ang(v), 190, 180 if major else 185, 2.2 if major else 1.6)
    for v in range(0, 161, 20):
        a = math.radians(ang(v))
        cv.text_center("dim", str(v), 14, C + 160 * math.sin(a), C - 160 * math.cos(a), weight=500)
    cv.text("dim", "MPH", 11, C, 292)
    vbox = (140, 186, 200, 92)
    if preview_mph is not None:
        fill_preview(cv, C, C, R_MID, W, A0, ang(preview_mph))
        number(cv, "%d" % preview_mph, vbox, 76)
    layout = dict(size=[480, 480], arc=arc_spec(C, C, R_MID, W, A0, A1, rng=(0, 160)),
                  value_box=dict(zip("xywh", vbox)), value_digit_px=76, indicator_color=HEX(ACCENT))
    return cv.render(), layout


# ------------------------------------------------------------------ small pictograms
def fuel_icon(cv, ink, cx, cy, u=1.0):
    x, y = cx - 8 * u, cy - 11 * u
    cv.rrect(ink, x, y, x + 12 * u, y + 21 * u, 2 * u, 2.2 * u)
    cv.line(ink, [(x + 3 * u, y + 6 * u), (x + 9 * u, y + 6 * u)], 2 * u)
    cv.line(ink, [(x - 2 * u, y + 21 * u), (x + 14 * u, y + 21 * u)], 2.2 * u)
    cv.line(ink, [(x + 12 * u, y + 6 * u), (x + 16 * u, y + 9 * u), (x + 16 * u, y + 17 * u), (x + 13.5 * u, y + 17 * u)], 2 * u)


def temp_icon(cv, ink, cx, cy, u=1.0):
    cv.rrect(ink, cx - 3 * u, cy - 15 * u, cx + 3 * u, cy + 3 * u, 3 * u, 2.2 * u)
    cv.ring(ink, cx, cy + 7 * u, 5.5 * u, 2.2 * u)
    for dy in (-11, -6):
        cv.line(ink, [(cx + 6 * u, cy + dy * u), (cx + 10 * u, cy + dy * u)], 1.8 * u)
    pts = [(cx - 10 * u + i * u, cy + 18 * u + 1.6 * u * math.sin(i / 2.2)) for i in range(21)]
    cv.line(ink, pts, 2 * u)


# ------------------------------------------------------------------ side 480
def side(preview=None):
    cv = Canvas(480, SS=4)
    cv.add_layer("xt", RED_TRACK)
    C = 240
    R_MID, W = 218, 12
    # fuel (left): bottom -140 -> top -40
    F0, F1 = -140.0, -40.0
    reserve = F0 + (F1 - F0) * 0.125
    cv.arc_track("xt", C, C, R_MID, W, F0, reserve)
    cv.arc_track("seg", C, C, R_MID, W, reserve, F1)
    cv.sector("seg", C, C, R_MID + W / 2, R_MID - W / 2, reserve, reserve + 2)
    cv.tick("dim", C, C, -90, R_MID - 10, R_MID - 17, 2)                    # 1/2
    # water (right): bottom 140 -> top 40, 190F at 90 (middle), warn zone from 211F (top 20 %)
    W0, W1 = 140.0, 40.0
    warn = W0 + (W1 - W0) * 0.8
    cv.arc_track("seg", C, C, R_MID, W, warn, W0)
    cv.arc_track("xt", C, C, R_MID, W, W1, warn)
    cv.sector("seg", C, C, R_MID + W / 2, R_MID - W / 2, warn - 2, warn)
    cv.tick("w", C, C, 90, R_MID - 10, R_MID - 19, 2.4)                      # 190F
    for txt, a, ink in (("E", -149, "x"), ("F", -31, "dim"), ("C", 149, "dim"), ("H", 31, "x")):
        cv.text_center(ink, txt, 10, C + R_MID * math.sin(math.radians(a)), C - R_MID * math.cos(math.radians(a)))
    fuel_icon(cv, "dim", 68, 240)
    temp_icon(cv, "dim", 412, 236)
    # boost
    cv.text("dim", "BOOST", 9, 228, 148, anchor="c")
    cv.text("r", "PSI", 9, 278, 148, anchor="c")
    bbox = (140, 164, 200, 76)
    cv.line("hair", [(214, 256), (266, 256)], 1.6)
    # lower pair
    cv.text("dim", "AIR", 8, 162, 282)
    cv.text("r", "°F", 8, 190, 282)
    cv.text("dim", "BATTERY", 8, 292, 282)
    cv.text("r", "V", 8, 334, 282)
    cv.line("hair", [(240, 284), (240, 330)], 1.4)
    ibox, vbox = (122, 296, 108, 40), (250, 296, 108, 40)
    if preview:
        boost, iat, volts, water_lit, fuel_lit = preview
        fill_preview(cv, C, C, R_MID, W, F0, F0 + (F1 - F0) * fuel_lit)
        fill_preview(cv, C, C, R_MID, W, W0, W0 + (W1 - W0) * water_lit, "x" if water_lit > 0.8 else "r")
        number(cv, boost, bbox, 54)
        number(cv, iat, ibox, 28)
        number(cv, volts, vbox, 28)
    layout = dict(size=[480, 480],
                  fuel_arc=arc_spec(C, C, R_MID, W, F0, F1, rng=(0, 100)),
                  water_arc=dict(arc_spec(C, C, R_MID, W, W1, W0, reverse=True, rng=(0, 100)),
                                 mapping="60F->0, 190F->50 (middle tick), 225F->100; warn (red) above 80 = ~211F"),
                  boost_box=dict(zip("xywh", bbox)), boost_digit_px=54,
                  iat_box=dict(zip("xywh", ibox)), battery_box=dict(zip("xywh", vbox)), small_digit_px=28)
    return cv.render(), layout


# ------------------------------------------------------------------ air 480 (display)
CORNERS = {  # key: (a_start (empty end), a_end (full end))  fills grow from the sides towards front/rear
    "fl": (-78.0, -14.0), "fr": (78.0, 14.0), "rr": (102.0, 166.0), "rl": (-102.0, -166.0)}


def air(preview=None):
    cv = Canvas(480, SS=4)
    C = 240
    R_MID, W = 216, 12
    for key, (a, b) in CORNERS.items():
        cv.arc_track("seg", C, C, R_MID, W, min(a, b), max(a, b))
    cv.text("dim", "FRONT", 8, C, 18)
    cv.text("dim", "REAR", 8, C, 452)
    pos = {"fl": (148, 150), "fr": (332, 150), "rl": (148, 318), "rr": (332, 318)}
    boxes = {}
    for key, (x, y) in pos.items():
        cv.text("dim", key.upper(), 9, x, y - 34)
        boxes[key] = (x - 56, y - 20, 112, 46)
    cv.text("dim", "TANK", 9, C, 196)
    cv.text("r", "PSI", 9, C, 276)
    tbox = (160, 210, 160, 58)
    cv.line("hair", [(190, 98), (290, 98)], 1.2) if False else None
    if preview:
        tank, vals = preview
        number(cv, tank, tbox, 44)
        for key, (txt, frac) in vals.items():
            a, b = CORNERS[key]
            fill_preview(cv, C, C, R_MID, W, a, a + (b - a) * frac)
            number(cv, txt, boxes[key], 30)
    layout = dict(size=[480, 480], tank_box=dict(zip("xywh", tbox)), tank_digit_px=44,
                  corner_boxes={k: dict(zip("xywh", v)) for k, v in boxes.items()}, corner_digit_px=30,
                  corner_arcs={k: arc_spec(C, C, R_MID, W, a, b, reverse=(b < a), rng=(0, 150))
                               for k, (a, b) in CORNERS.items()})
    # for reverse arcs LVGL wants bg_start < bg_end going clockwise, so swap the printed ends
    for k, (a, b) in CORNERS.items():
        lo, hi = min(a, b), max(a, b)
        layout["corner_arcs"][k].update(bg_start=to_lvgl(lo), bg_end=to_lvgl(hi))
    return cv.render(), layout


def main():
    import os
    os.makedirs("previews", exist_ok=True)
    out = {}
    img, out["tach"] = tach()
    img.save("tach_ek_800.png")
    tach(6500)[0].save("tach_ek_800_preview.png")
    tach(8400)[0].save(os.path.join("previews", "tach_redline_preview.png"))
    img, out["speedo"] = speedo()
    img.save("speedo_480_round.png")
    speedo(68)[0].save("speedo_480_round_preview.png")
    img, out["side"] = side()
    img.save("side_480_round.png")
    side(("11.2", "96", "14.1", 0.5, 0.62))[0].save("side_480_round_preview.png")
    img, out["air"] = air()
    img.save("air_480_round.png")
    air(("165", {"fl": ("82", 0.55), "fr": ("84", 0.56), "rl": ("68", 0.45), "rr": ("70", 0.47)}))[0].save(
        "air_480_round_preview.png")
    with open("gauges_layout.json", "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2, ensure_ascii=False)
    print("gauges done")


if __name__ == "__main__":
    main()
