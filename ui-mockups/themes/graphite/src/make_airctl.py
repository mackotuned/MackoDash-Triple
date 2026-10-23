"""
Air ride CONTROL mode - GRAPHITE theme. Same layout, tiles and coordinates as the classic theme;
only colours, font, face and stroke style change.

  RIGHT screen 480x480 (touch) : corner selector (car + name, < > buttons / swipe), UP, DOWN, AIR OUT (hold), EXIT
  CENTER screen 800x800        : tank pressure + bar, four corner pressures around a top-view car,
                                 controlled corners lit red, compressor button (OFF / AUTO / RUN)

Everything static is in the backgrounds. Things that change are TILES: small images cut from fully rendered
state pages, so they drop in at fixed x,y with the texture lining up perfectly.
LVGL only swaps tile images, lights tank segments and draws pressure numbers.

Run:  python make_airctl.py   ->  airctl/ (backgrounds, tiles, previews, airctl_layout.json)
"""
import json
import math
import os
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops

OUT_DIR = "airctl"
from theme import Canvas, TEXT as INK_WHITE, ACCENT as INK_RED, NUMBER_WEIGHT   # GRAPHITE theme
NUM_SQUEEZE = 1.0

GROUPS = [("all", "ALL", ("fl", "fr", "rl", "rr")), ("front", "FRONT", ("fl", "fr")),
          ("rear", "REAR", ("rl", "rr")), ("fl", "FRONT LEFT", ("fl",)), ("fr", "FRONT RIGHT", ("fr",)),
          ("rl", "REAR LEFT", ("rl",)), ("rr", "REAR RIGHT", ("rr",))]


# ------------------------------------------------------------------ shared pictograms
def car_top(cv, cx, cy, h, sel=(), detail=True, ink="w", front_marker=True):
    """Top-view car, front up, height h px. sel wheels filled red."""
    u = h / 100.0
    X = lambda x: cx + x * u
    Y = lambda y: cy + y * u
    w = 2.2 if h < 150 else 1.6                      # stroke in units
    sw = max(1.6, w * u)
    cv.rrect(ink, X(-18), Y(-44), X(18), Y(44), 13 * u, sw)
    if detail:
        cv.line(ink, [(X(-13), Y(-15)), (X(-10), Y(-26)), (X(10), Y(-26)), (X(13), Y(-15)), (X(-13), Y(-15))], sw * 0.8)
        cv.line(ink, [(X(-12), Y(20)), (X(-9), Y(30)), (X(9), Y(30)), (X(12), Y(20)), (X(-12), Y(20))], sw * 0.8)
        cv.line(ink, [(X(-12), Y(-15)), (X(-12), Y(20))], sw * 0.6)
        cv.line(ink, [(X(12), Y(-15)), (X(12), Y(20))], sw * 0.6)
    else:
        cv.line(ink, [(X(-12), Y(-16)), (X(12), Y(-16))], sw * 0.8)
        cv.line(ink, [(X(-12), Y(20)), (X(12), Y(20))], sw * 0.8)
    wheels = {}
    for key, x, y in (("fl", -29, -26), ("fr", 29, -26), ("rl", -29, 26), ("rr", 29, 26)):
        box = (X(x - 7), Y(y - 13), X(x + 7), Y(y + 13))
        if key in sel:
            cv.rrect("r", *box, 4 * u, fill=True)
        else:
            cv.rrect(ink, X(x - 6), Y(y - 12), X(x + 6), Y(y + 12), 3.5 * u, sw * 0.85)
        wheels[key] = [round(v) for v in box]
    if front_marker:
        cv.poly("dim", [(X(-6), Y(-52)), (X(6), Y(-52)), (X(0), Y(-58))])  # front marker
    return wheels


def chevron(cv, ink, cx, cy, size, direction, w):
    d = -1 if direction == "left" else 1
    cv.line(ink, [(cx - d * size * 0.35, cy - size * 0.6), (cx + d * size * 0.35, cy), (cx - d * size * 0.35, cy + size * 0.6)], w)


def arrow_big(cv, ink, cx, cy, size, up=True, w=None):
    d = -1 if up else 1
    w = w or size * 0.22
    cv.line(ink, [(cx - size * 0.55, cy - d * size * 0.28), (cx, cy + d * size * 0.32), (cx + size * 0.55, cy - d * size * 0.28)], w)


def air_out_icon(cv, ink, cx, cy, size):
    """Bellows with three outward puffs."""
    u = size / 100
    cv.rrect(ink, cx - 22 * u, cy - 30 * u, cx + 22 * u, cy - 4 * u, 12 * u, 7 * u)
    cv.rrect(ink, cx - 22 * u, cy - 4 * u, cx + 22 * u, cy + 22 * u, 12 * u, 7 * u)
    for dx in (-1, 1):
        for k, dy in enumerate((-18, 0, 18)):
            x0 = cx + dx * 32 * u
            cv.line(ink, [(x0, cy + dy * u - 4 * u), (x0 + dx * 12 * u, cy + dy * u - 4 * u)], 6 * u)


def compressor_icon(cv, ink, cx, cy, size):
    """Small air compressor: motor cylinder with fins + pump head + outlet."""
    u = size / 100
    w = 6 * u
    cv.rrect(ink, cx - 40 * u, cy - 6 * u, cx + 8 * u, cy + 26 * u, 10 * u, w)          # motor
    for x in (-28, -16, -4):
        cv.line(ink, [(cx + x * u, cy - 2 * u), (cx + x * u, cy + 22 * u)], 4 * u)
    cv.rrect(ink, cx + 8 * u, cy - 24 * u, cx + 34 * u, cy + 26 * u, 6 * u, w)           # pump head
    cv.line(ink, [(cx + 21 * u, cy - 24 * u), (cx + 21 * u, cy - 36 * u), (cx + 40 * u, cy - 36 * u)], w)
    cv.line(ink, [(cx - 44 * u, cy + 34 * u), (cx + 40 * u, cy + 34 * u)], w)             # base


# ------------------------------------------------------------------ RIGHT: 480 control page
R480 = dict(
    selector_tile=(124, 44, 356, 196),            # x0,y0,x1,y1 of the car+name tile
    car=(240, 106, 104),                          # cx, cy, height
    name_y=164,
    prev_btn=(78, 116, 30), next_btn=(402, 116, 30),     # cx, cy, r
    up_btn=(128, 206, 352, 272), down_btn=(128, 282, 352, 348),
    airout_btn=(108, 362, 236, 416), exit_btn=(244, 362, 372, 416),
)


def btn_outline(cv, box, ink, pressed):
    x0, y0, x1, y1 = box
    if pressed:
        cv.rrect(ink, x0, y0, x1, y1, 14, fill=True)
    else:
        cv.rrect(ink, x0, y0, x1, y1, 14, 2.0)


def control_480(group="all", pressed=None):
    cv = Canvas(480)
    L = R480
    cv.text("dim", "AIR RIDE  CONTROL", 8.5, 240, 30)
    # selector
    sel = dict((k, s) for k, _, s in GROUPS)[group]
    name = dict((k, n) for k, n, _ in GROUPS)[group]
    car_top(cv, *L["car"], sel=sel, detail=False)
    cv.text("w", name, 13, 240, L["name_y"], max_w=200)
    for key, d in (("prev_btn", "left"), ("next_btn", "right")):
        cx, cy, r = L[key]
        if pressed == key:
            cv.disc("w", cx, cy, r)
            chevron(cv, "k", cx, cy, r * 0.75, d, 4)
        else:
            cv.ring("w", cx, cy, r, 2.0)
            chevron(cv, "w", cx, cy, r * 0.75, d, 4)
    # up / down
    for key, up, label in (("up_btn", True, "UP"), ("down_btn", False, "DOWN")):
        box = L[key]
        p = pressed == key
        btn_outline(cv, box, "w", p)
        ink = "k" if p else "w"
        cy = (box[1] + box[3]) / 2
        arrow_big(cv, ink, box[0] + 56, cy, 34, up=up, w=7)
        cv.text(ink, label, 16, box[0] + 100, cy - 8, anchor="l")
    # air out (red, hold) / exit
    box = L["airout_btn"]
    p = pressed == "airout_btn"
    btn_outline(cv, box, "x", p)
    ink = "k" if p else "x"
    cx = (box[0] + box[2]) / 2
    cv.text(ink, "AIR OUT", 11, cx, box[1] + 15)
    cv.text("k" if p else "dim", "HOLD", 6.5, cx, box[1] + 33)
    box = L["exit_btn"]
    p = pressed == "exit_btn"
    btn_outline(cv, box, "w", p)
    ink = "k" if p else "w"
    cx = (box[0] + box[2]) / 2
    cv.text(ink, "EXIT", 11, cx, box[1] + 21)
    return cv.render()


# ------------------------------------------------------------------ CENTER: 800 pressure page
C800 = dict(
    tank_arc=dict(r_out=383, r_in=362, a0=-52, a1=52, n=13),
    tank_label_y=104, tank_value=(290, 120, 220, 74), tank_unit_y=204,
    car=(400, 418, 330),
    car_tile=(268, 226, 532, 618),
    corner_value={"fl": (88, 300, 190, 72), "fr": (522, 300, 190, 72),
                  "rl": (88, 488, 190, 72), "rr": (522, 488, 190, 72)},
    comp_tile=(296, 626, 504, 786),
    comp_btn=(400, 688, 50),
)


def pressure_800(group=None, comp="off"):
    cv = Canvas(800, SS=3)
    L = C800
    # tank bar along the top edge
    ta = L["tank_arc"]
    seg = (ta["a1"] - ta["a0"]) / ta["n"]
    tank_segs = []
    for i in range(ta["n"]):
        a = ta["a0"] + i * seg
        tank_segs.append((a, a + seg))
    cv.arc_track("seg", 400, 400, (ta["r_out"] + ta["r_in"]) / 2, ta["r_out"] - ta["r_in"], ta["a0"], ta["a1"])
    for txt, a in (("0", ta["a0"] - 6), ("200", ta["a1"] + 7)):
        r = (ta["r_out"] + ta["r_in"]) / 2
        cv.text_center("dim", txt, 11, 400 + r * math.sin(math.radians(a)), 400 - r * math.cos(math.radians(a)) + 4)
    cv.text("dim", "TANK", 12, 400, L["tank_label_y"])
    cv.text("r", "PSI", 13, 400, L["tank_unit_y"])
    # corner labels (value drawn by LVGL in the box below each)
    for key, lab in (("fl", "FL"), ("fr", "FR"), ("rl", "RL"), ("rr", "RR")):
        x, y, w, h = L["corner_value"][key]
        cx = x + w / 2
        cv.text("dim", lab, 12, cx - 22, y - 30, anchor="c")
        cv.text("r", "PSI", 12, cx + 26, y - 30, anchor="c")
    # car + controlled corners + name
    sel = dict((k, s) for k, _, s in GROUPS).get(group, ())
    wheels = car_top(cv, *L["car"], sel=sel, detail=True, front_marker=False)
    if group:
        name = dict((k, n) for k, n, _ in GROUPS)[group]
        cv.text("r", name, 13, 400, 596, max_w=250)
    else:
        cv.text("dim", "SELECT CORNER", 10, 400, 598)
    # compressor button
    cx, cy, r = L["comp_btn"]
    if comp == "run":
        cv.disc("r", cx, cy, r)
        compressor_icon(cv, "k", cx, cy + 2, 62)
        cv.text("r", "RUNNING", 12, cx, cy + r + 18)
    elif comp == "auto":
        cv.ring("w", cx, cy, r, 3.2)
        compressor_icon(cv, "w", cx, cy + 2, 62)
        cv.text("w", "AUTO", 12, cx, cy + r + 18)
    else:
        cv.ring("dim", cx, cy, r, 3.2)
        compressor_icon(cv, "dim", cx, cy + 2, 62)
        cv.text("dim", "COMP OFF", 12, cx, cy + r + 18)
    return cv.render(), tank_segs, wheels


# ------------------------------------------------------------------ export
def crop(img, box):
    return img.crop(box)


def sample_numbers(img, D, items, cap, ink=INK_WHITE, squeeze=NUM_SQUEEZE, red=()):
    cv = Canvas(D, SS=3)
    big = img.resize((cv.S, cv.S), Image.LANCZOS)
    w, r = Image.new("L", big.size, 0), Image.new("L", big.size, 0)
    for key, (txt, (x, y, bw, bh)) in items.items():
        g = cv.text_mask(txt, cap, squeeze, weight=NUMBER_WEIGHT)
        tgt = r if key in red else w
        tgt.paste(255, (int(cv.s(x + bw / 2) - g.width / 2), int(cv.s(y + bh / 2) - g.height / 2)), g)
    big = Image.composite(Image.new("RGB", big.size, ink), big, w.filter(ImageFilter.GaussianBlur(1)))
    big = Image.composite(Image.new("RGB", big.size, INK_RED), big, r.filter(ImageFilter.GaussianBlur(1)))
    return big.resize((D, D), Image.LANCZOS)


def light_tank(img, segs, n):
    cv = Canvas(800, SS=3)
    big = img.resize((cv.S, cv.S), Image.LANCZOS)
    ta = C800["tank_arc"]
    for i in range(n):
        cv.sector("r", 400, 400, ta["r_out"], ta["r_in"], *segs[i])
    for k, col in (("w", INK_WHITE), ("r", INK_RED)):
        big = Image.composite(Image.new("RGB", big.size, col), big, cv.m[k].filter(ImageFilter.GaussianBlur(1)))
    return big.resize((800, 800), Image.LANCZOS)


def to_lvgl(d12):
    return round((d12 + 270) % 360, 2)


def main():
    os.makedirs(os.path.join(OUT_DIR, "tiles"), exist_ok=True)
    os.makedirs(os.path.join(OUT_DIR, "previews"), exist_ok=True)
    layout = {"right_480": {}, "center_800": {}}

    # ---------- right 480 ----------
    bg = control_480("all")
    bg.save(os.path.join(OUT_DIR, "control_480_bg.png"))
    st = R480["selector_tile"]
    tiles = {}
    for key, name, _ in GROUPS:
        img = control_480(key)
        crop(img, st).save(os.path.join(OUT_DIR, "tiles", "sel_%s.png" % key))
        tiles[key] = "tiles/sel_%s.png" % key
        if key in ("fl", "front"):
            img.save(os.path.join(OUT_DIR, "previews", "control_480_%s.png" % key))
    btns = {}
    for b in ("prev_btn", "next_btn", "up_btn", "down_btn", "airout_btn", "exit_btn"):
        img = control_480("all", pressed=b)
        if b in ("prev_btn", "next_btn"):
            cx, cy, r = R480[b]
            box = (cx - r - 4, cy - r - 4, cx + r + 4, cy + r + 4)
        else:
            x0, y0, x1, y1 = R480[b]
            box = (x0 - 3, y0 - 3, x1 + 3, y1 + 3)
        crop(img, box).save(os.path.join(OUT_DIR, "tiles", "pressed_%s.png" % b.replace("_btn", "")))
        btns[b.replace("_btn", "")] = dict(touch_box=list(box), pressed_tile="tiles/pressed_%s.png" % b.replace("_btn", ""))
        if b == "up_btn":
            img.save(os.path.join(OUT_DIR, "previews", "control_480_up_pressed.png"))
    layout["right_480"] = dict(
        background="control_480_bg.png", size=[480, 480],
        selector=dict(tile_xy=list(st[:2]), tile_size=[st[2] - st[0], st[3] - st[1]], tiles=tiles,
                      touch_box=list(st), order=[k for k, _, _ in GROUPS]),
        buttons=btns)

    # ---------- center 800 ----------
    bg, tank_segs, wheels = pressure_800(None, "off")
    bg.save(os.path.join(OUT_DIR, "pressure_800_bg.png"))
    ct = C800["car_tile"]
    car_tiles = {"none": "tiles/car_none.png"}
    crop(bg, ct).save(os.path.join(OUT_DIR, "tiles", "car_none.png"))
    full = {}
    for key, _, _ in GROUPS:
        img, _, _ = pressure_800(key, "off")
        crop(img, ct).save(os.path.join(OUT_DIR, "tiles", "car_%s.png" % key))
        car_tiles[key] = "tiles/car_%s.png" % key
        full[key] = img
    cpt = C800["comp_tile"]
    comp_tiles = {}
    for state in ("off", "auto", "run"):
        img, _, _ = pressure_800(None, state)
        crop(img, cpt).save(os.path.join(OUT_DIR, "tiles", "comp_%s.png" % state))
        comp_tiles[state] = "tiles/comp_%s.png" % state
    cx, cy, r = C800["comp_btn"]
    layout["center_800"] = dict(
        background="pressure_800_bg.png", size=[800, 800],
        car=dict(tile_xy=list(ct[:2]), tile_size=[ct[2] - ct[0], ct[3] - ct[1]], tiles=car_tiles),
        compressor=dict(tile_xy=list(cpt[:2]), tile_size=[cpt[2] - cpt[0], cpt[3] - cpt[1]], tiles=comp_tiles,
                        touch_box=[cx - r - 10, cy - r - 10, cx + r + 10, cy + r + 10]),
        tank_value_box=dict(zip("xywh", C800["tank_value"])),
        corner_value_boxes={k: dict(zip("xywh", v)) for k, v in C800["corner_value"].items()},
        tank_ring=dict(center=[400, 400], radius_outer=C800["tank_arc"]["r_out"],
                       width=C800["tank_arc"]["r_out"] - C800["tank_arc"]["r_in"], range=[0, 200],
                       segments=[dict(start=to_lvgl(a), end=to_lvgl(b)) for a, b in tank_segs]))

    # ---------- previews with sample numbers ----------
    img, _, _ = pressure_800("fl", "auto")
    img = light_tank(img, tank_segs, 11)
    vals = {"tank": ("162", C800["tank_value"]), "fl": ("78", C800["corner_value"]["fl"]),
            "fr": ("84", C800["corner_value"]["fr"]), "rl": ("70", C800["corner_value"]["rl"]),
            "rr": ("71", C800["corner_value"]["rr"])}
    sample_numbers(img, 800, vals, 50, red=("fl",)).save(os.path.join(OUT_DIR, "previews", "pressure_800_fl_auto.png"))
    img, _, _ = pressure_800("rear", "run")
    img = light_tank(img, tank_segs, 8)
    vals["tank"] = ("118", C800["tank_value"])
    sample_numbers(img, 800, vals, 50, red=("rl", "rr")).save(os.path.join(OUT_DIR, "previews", "pressure_800_rear_run.png"))

    with open(os.path.join(OUT_DIR, "airctl_layout.json"), "w") as f:
        json.dump(layout, f, indent=2)
    print("done")


if __name__ == "__main__":
    main()
