# Hardware Plan

## Confirmed Display Classes

| Role | MCU | Display | Resolution | Initial responsibility |
|---|---|---|---:|---|
| Center | ESP32-P4NRW32 | Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C | 800x800 | Primary analog gauges, warnings, settings |
| Left | ESP32-S3 | Waveshare ESP32-S3-Touch-LCD-2.8C | 480x480 | Assignable secondary gauges |
| Right | ESP32-S3 | Waveshare ESP32-S3-Touch-LCD-2.8C | 480x480 | Assignable secondary gauges |

## Wiring Rules

- Left link: P4 `GPIO30` TX to left S3 printed `RXD` (`GPIO44`).
- Right link: P4 `GPIO31` TX to right S3 printed `RXD` (`GPIO44`).
- Connect a common ground on each one-way link; no S3 TX wire is required.
- Both links use 460800 baud, 8 data bits, no parity, and 1 stop bit.
- Keep display, touch, SD, USB, and boot-strapping pins out of the UART allocation.
- Power each screen from an appropriately sized regulated automotive supply.
- Do not power a complete display assembly from another board's logic rail.

## Confirmed Board References

- The P4 schematic identifies PCB design Rev1.1; a visible PCB revision marking is not required.
- The connected ESP32-P4 reports silicon revision v1.3; center builds select the compatible Rev1.0+ profile and 360 MHz CPU frequency.
- The connected center board reports 32 MB flash, matching its ESP32-P4NRW32 hardware profile.
- The connected left ESP32-S3 reports silicon revision v0.2, 8 MB embedded octal PSRAM, and 16 MB quad flash.
- P4 J8 exposes `GPIO30` and `GPIO31` directly on the 40-pin header.
- S3 printed `RXD` is `GPIO44`; printed `TXD` is `GPIO43` and is unused here.
- Center display support uses `waveshare/esp32_p4_wifi6_touch_lcd_xc` version `3.0.1`.

## Information Still Needed

- Cable lengths and whether the boards share one regulated supply.
- Desired gauge assignment for center, left, and right at first boot.
