from pathlib import Path
import shutil

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
GRAPHITE = ROOT / "ui-mockups" / "themes" / "graphite"


def rgb565(source: Path, destination: Path, size: tuple[int, int]) -> None:
    image = Image.open(source).convert("RGB")
    if image.size != size:
        raise ValueError(f"{source} is {image.size}, expected {size}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    output = bytearray(size[0] * size[1] * 2)
    offset = 0
    for red, green, blue in image.get_flattened_data():
        value = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
        output[offset] = value >> 8
        output[offset + 1] = value & 0xFF
        offset += 2
    destination.write_bytes(output)


def copy_png(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def main() -> None:
    center = ROOT / "apps" / "center_p4" / "main" / "assets" / "themes" / "graphite"
    copy_png(GRAPHITE / "tach_ek_800.png", center / "graphite_tach_ek_800.png")
    copy_png(GRAPHITE / "airctl" / "pressure_800_bg.png", center / "airctl" / "graphite_pressure_800_bg.png")
    for name in ("car_none", "car_all", "car_front", "car_rear", "car_fl", "car_fr", "car_rl", "car_rr",
                 "comp_off", "comp_auto", "comp_run"):
        copy_png(GRAPHITE / "airctl" / "tiles" / f"{name}.png", center / "airctl" / f"graphite_{name}.png")

    rgb565(GRAPHITE / "side_480_round.png",
           ROOT / "components" / "side_pages" / "assets" / "graphite_side_480_rgb565.bin", (480, 480))
    rgb565(GRAPHITE / "speedo_480_round.png",
           ROOT / "components" / "side_display" / "assets" / "graphite_speedo_480_rgb565.bin", (480, 480))
    rgb565(GRAPHITE / "climate480" / "climate_480_bg.png",
           ROOT / "components" / "climate_display" / "assets" / "graphite_climate_480_rgb565.bin", (480, 480))
    rgb565(GRAPHITE / "air_480_round.png",
           ROOT / "components" / "side_pages" / "assets" / "graphite_air_480_rgb565.bin", (480, 480))
    rgb565(GRAPHITE / "airctl" / "control_480_bg.png",
           ROOT / "components" / "side_pages" / "assets" / "airctl" / "graphite_control_480_bg.bin", (480, 480))
    for name in ("sel_all", "sel_front", "sel_rear", "sel_fl", "sel_fr", "sel_rl", "sel_rr",
                 "pressed_prev", "pressed_next", "pressed_up", "pressed_down", "pressed_airout", "pressed_exit"):
        source = GRAPHITE / "airctl" / "tiles" / f"{name}.png"
        image = Image.open(source)
        rgb565(source, ROOT / "components" / "side_pages" / "assets" / "airctl" / f"graphite_{name}.bin", image.size)

    knob_source = GRAPHITE / "knob_ui"
    knob_destination = ROOT / "apps" / "knob_s3" / "main" / "assets" / "rgb565"
    classic_names = {path.stem for path in knob_destination.glob("*.bin") if not path.stem.startswith("graphite_")}
    missing = sorted(name for name in classic_names if not (knob_source / f"{name}.png").exists())
    if missing:
        raise FileNotFoundError(f"Graphite Knob assets missing: {', '.join(missing)}")
    for name in sorted(classic_names):
        rgb565(knob_source / f"{name}.png", knob_destination / f"graphite_{name}.bin", (240, 240))

    print(f"Exported Graphite runtime assets for {len(classic_names)} Knob pages")


if __name__ == "__main__":
    main()
