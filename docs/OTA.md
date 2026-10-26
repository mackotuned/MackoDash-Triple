# Over-the-air updates

Every board updates over Wi-Fi through the center P4, so the dash never has
to come apart to flash firmware.

## One-time setup (USB)

Each board needs one last USB flash to install the firmware that supports
updates. This also writes the new flash layout (two app slots and an update
record). Settings and the odometer live in NVS at the same address and are
kept.

```sh
cd apps/center_p4 && idf.py -p COMx flash
cd apps/left_s3 && idf.py -p COMx flash
cd apps/right_s3 && idf.py -p COMx flash
cd apps/knob_s3 && idf.py -p COMx flash
cd private/apps/climate_esp32 && idf.py -p COMx flash
```

After this, never use USB again unless a board is completely unresponsive.

## Updating

Build the firmware, join the `MackoDash` Wi-Fi network from the laptop, then:

```sh
python tools/ota_flash.py              # every board
python tools/ota_flash.py left right   # only the side displays
```

The tool uploads one image at a time to the P4 and waits for that board to
install it. Order matters: the climate node goes before the left display
(its gateway), and the P4 goes last because restarting it drops the network.

The phone settings page has the same feature under System → Firmware: choose
the board, pick its `.bin` from the build folder, and tap Upload.

## How it works

1. The image is uploaded to `POST /api/ota/upload?target=NAME` on the P4,
   which keeps it in PSRAM and checks the chip type and project name so an
   image can never go to the wrong board.
2. For itself, the P4 writes the image to its spare app slot and restarts.
3. For any other board, the P4 sends a `CLUSTER_MSG_OTA_OFFER`. The board
   downloads from `http://192.168.4.1:8070/image` (a separate server, so the
   phone UI stays responsive) using `components/cluster_ota`.
4. The climate node is not on the network; the left display forwards the
   offer over its private link and the node joins the network just long enough to
   download.
5. Boards post progress to `POST /api/ota/report`; `GET /api/ota/status`
   shows it.

## Safety

- The bootloader checks the image's SHA-256 before switching to it.
- Rollback is enabled. A new firmware must confirm itself once it is running
  properly (P4: display and network up; displays and knob: network joined;
  climate node: relays driven and radio up). If it crashes or resets first,
  the bootloader returns to the previous firmware.
- Only one update runs at a time; the P4 refuses a new upload while a board
  is still downloading.
