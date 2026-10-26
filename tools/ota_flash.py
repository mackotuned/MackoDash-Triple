"""Update MackoDash boards over Wi-Fi through the center P4.

Join the MackoDash Wi-Fi network, build the firmware, then run:

    python tools/ota_flash.py            # every board
    python tools/ota_flash.py left knob  # only these
    python tools/ota_flash.py right=path/to/other.bin

Boards update one at a time. The climate node goes before its gateway (the
left display) and the P4 goes last because it restarts the network.
See docs/OTA.md.
"""
import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ORDER = ["climate", "knob", "right", "left", "center"]
IMAGES = {
    "center": ROOT / "apps/center_p4/build/mackodash_triple_center.bin",
    "left": ROOT / "apps/left_s3/build/mackodash_triple_left.bin",
    "right": ROOT / "apps/right_s3/build/mackodash_triple_right.bin",
    "knob": ROOT / "apps/knob_s3/build/mackodash_knob.bin",
    "climate": ROOT / "private/apps/climate_esp32/build/mackodash_hvac_node.bin",
}
UPDATE_TIMEOUT_S = 240


def request(host, method, path, body=None, timeout=10):
    req = urllib.request.Request(f"http://{host}{path}", data=body, method=method)
    if body is not None:
        req.add_header("Content-Type", "application/octet-stream")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            return response.status, json.loads(response.read() or b"{}")
    except urllib.error.HTTPError as error:
        try:
            return error.code, json.loads(error.read() or b"{}")
        except ValueError:
            return error.code, {}


def update(host, target, image):
    data = image.read_bytes()
    print(f"{target:8} uploading {len(data) / 1e6:.2f} MB from {image.name}")
    code, reply = request(host, "POST", f"/api/ota/upload?target={target}",
                          data, timeout=180)
    if code not in (200, 202):
        print(f"{target:8} rejected: {reply.get('message', code)}")
        return False
    if target == "center":
        print(f"{target:8} installed; the P4 is restarting")
        return True

    deadline = time.time() + UPDATE_TIMEOUT_S
    last = None
    while time.time() < deadline:
        time.sleep(1)
        try:
            _, status = request(host, "GET", "/api/ota/status", timeout=5)
        except OSError:
            continue  # the radio is busy; try again
        if status.get("target") != target:
            continue
        line = f"{status.get('state')} {status.get('percent', 0)}% {status.get('message', '')}".strip()
        if line != last:
            print(f"{target:8} {line}")
            last = line
        if status.get("state") == "done":
            return True
        if status.get("state") == "error":
            return False
    print(f"{target:8} timed out")
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("targets", nargs="*",
                        help="boards to update (default: all), optionally NAME=IMAGE")
    parser.add_argument("--host", default="192.168.4.1", help="P4 address")
    args = parser.parse_args()

    chosen = {}
    for item in args.targets or ORDER:
        name, _, path = item.partition("=")
        if name not in IMAGES:
            parser.error(f"unknown board '{name}'; choose from {', '.join(ORDER)}")
        chosen[name] = Path(path) if path else IMAGES[name]
    missing = [str(path) for path in chosen.values() if not path.exists()]
    if missing:
        parser.error("build first; missing " + ", ".join(missing))

    for target in [name for name in ORDER if name in chosen]:
        if not update(args.host, target, chosen[target]):
            print("Stopped. Fix the problem above and run again.")
            return 1
        if target != "center":
            time.sleep(5)  # let the board restart and rejoin
    print("All requested boards updated.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
