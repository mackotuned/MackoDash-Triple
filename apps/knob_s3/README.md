# MackoDash Knob

ESP-IDF 5.5.3 application for the verified Elecrow CrowPanel 1.28-inch ESP32-S3
rotary display. It renders the 50 baked RGB565 screens in `main/assets/rgb565`
and uses the shared MackoDash Wi-Fi/UDP transport as role
`CLUSTER_WIRELESS_ROLE_KNOB`.

The knob joins the Center P4 `MackoDash` network on channel 6. Center heartbeat
loss produces an obvious OFFLINE overlay after 2.5 seconds and suppresses all
commands. Center Air Ride state updates supply live FL/FR/RL/RR pressure values.
The knob only talks to the Center P4; it never drives the Air Ride or climate
hardware directly. Climate pages use the board's real positions: fan off plus
10 speeds, temperature 0 (full cold) to 14 (full hot), and five modes.

## Controls and Flows

- Rotate or swipe left/right to wrap through menus or adjust values.
- Press or tap to select and confirm.
- Hold for at least 600 ms or swipe down to go back; adjust pages restore the
    old value and send CANCEL.
- After 20 seconds idle, menus return to Main item 0 and adjustments cancel.
- Record idle returns to Main without stopping an active recording.

The UI and protocol messages support screen source, recording, Air Ride targets,
HVAC fan/blend/mode, logs, and all-screen brightness. Brightness updates the
local GPIO46 PWM immediately; Center applies it to all three displays and saves
only the confirmed value. Center also relays heartbeat and Air Ride state. Screen
selection, recording, Air Ride pressure presets, Climate, and Logs commands are
decoded but remain pending until their authoritative backends are implemented.

## Verified Pin Map

| Function | GPIO |
| --- | ---: |
| LCD SCLK / MOSI / DC / CS / RST | 10 / 11 / 3 / 9 / 14 |
| Touch SDA / SCL / INT / RST | 6 / 7 / 5 / 13 |
| Encoder A / B / press | 45 / 42 / 41 |
| Backlight | 46 |
| Dashboard UART TX / RX | 43 / 44 |
| Board power enables | 1 / 2 |

Touch uses CST816 address `0x15`. The display is a 240x240 GC9A01 using the
verified legacy initialization sequence and direct DMA framebuffer transfer.

## Build

From the workspace root with ESP-IDF 5.5.3 exported:

```powershell
idf.py -C apps\knob_s3 build
```

Do not flash until the connected serial port and CrowPanel board identity have
been explicitly confirmed.