# Hardware Plan

## Confirmed Display Classes

| Role | MCU | Display | Resolution | Initial responsibility |
|---|---|---|---:|---|
| Center | ESP32-P4NRW32 | Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C | 800x800 | Fixed RPM tachometer, overlays, warnings, settings |
| Left | ESP32-S3 | Waveshare ESP32-S3-Touch-LCD-2.8C | 480x480 | CAN telemetry with three configurable values and fixed fuel/coolant arcs |
| Right | ESP32-S3 | Waveshare ESP32-S3-Touch-LCD-2.8C | 480x480 | Fixed Air Ride pressures with optional CAN battery voltage |

## Wiring Rules

- No external UART signal wires are required between the three displays.
- Leave P4 `GPIO30`, `GPIO31`, and `GPIO32` disconnected from the S3 `GPIO43/44`
	pins.
- The P4 hosts `MackoDash` on Wi-Fi channel 6; both S3 displays join that network.
- USB remains available independently on each board for flashing and service.
- Power each screen from an appropriately sized regulated automotive supply.
- Do not power a complete display assembly from another board's logic rail.
- Connect supply grounds according to the automotive power-distribution design;
	no additional signal ground lead is needed for the wireless cluster link.

## Confirmed Board References

- The P4 schematic identifies PCB design Rev1.1; a visible PCB revision marking is not required.
- The connected ESP32-P4 reports silicon revision v1.3; center builds select the compatible Rev1.0+ profile and 360 MHz CPU frequency.
- The connected center board reports 32 MB flash, matching its ESP32-P4NRW32 hardware profile.
- The connected left ESP32-S3 reports silicon revision v0.2, 8 MB embedded octal PSRAM, and 16 MB quad flash.
- The connected right ESP32-S3 reports the same v0.2, 8 MB PSRAM, and 16 MB flash profile.
- P4 J8 exposes `GPIO30`, `GPIO31`, and `GPIO32`; `GPIO32` is J8 pin 11.
- The external CAN transceiver uses P4 `GPIO4` for controller RX and `GPIO5` for controller TX.
- The P4 onboard microSD slot uses 4-bit SDMMC on `GPIO39`-`GPIO44` and stores
	Hondata sessions under `/MackoDash`.
- S3 printed `RXD` (`GPIO44`) and `TXD` (`GPIO43`) are unused by the cluster link.
- Side-display touch is GT911 at `0x5D` on SDA `GPIO15`, SCL `GPIO7`, and INT
	`GPIO16`; reset is TCA9554 EXIO2 (expander bit 1).
- Native S3 USB/JTAG flashing does not use GPIO43/44. The CH343 UART connector
	can also be used without isolating cluster wiring because no cluster UART is connected.
- The previous UART side links were validated with consecutive CRC-valid P4
	heartbeat frames. The wireless replacement still requires on-vehicle validation.
- Both side displays render stable LVGL gauges with 80 MHz octal PSRAM, 64-byte cache lines, triple framebuffers, and an RGB bounce buffer.
- Both side displays enter a red `OFFLINE` state within 2.5 seconds of heartbeat loss and recover automatically when frames resume.
- Center display support uses `waveshare/esp32_p4_wifi6_touch_lcd_xc` version `3.0.1`.
- The right S3 Air Ride bridge shares Wi-Fi channel 6 with the cluster link and
	treats Air Ride telemetry as offline after one second.

## Information Still Needed

- Cable lengths and whether the boards share one regulated supply.

## Display Design Sources

The production references and editable source package are retained in
[`ui-mockups/civic_tach`](ui-mockups/civic_tach/). The directory includes the
800x800 center tach, 480x480 telemetry and Air Ride PNGs, preview images, JSON
layout coordinates, implementation notes, and Python image generators.
