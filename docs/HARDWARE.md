# Hardware Plan

## Confirmed Display Classes

| Role | MCU | Display | Resolution | Initial responsibility |
|---|---|---|---:|---|
| Center | ESP32-P4 | Waveshare 3.4-inch round MIPI-DSI | 800x800 | Primary analog gauges, warnings, settings |
| Left | ESP32-S3 | Waveshare 2.8-inch round ST7701S/GT911 | 480x480 | Assignable secondary gauges |
| Right | ESP32-S3 | Waveshare 2.8-inch round ST7701S/GT911 | 480x480 | Assignable secondary gauges |

## Wiring Rules

- Use separate P4 UART peripherals and cables for left and right displays.
- Cross TX to RX and connect a common ground on each link.
- Do not select GPIOs until each exact board schematic and revision is archived.
- Keep display, touch, SD, USB, and boot-strapping pins out of the UART allocation.
- Power each screen from an appropriately sized regulated automotive supply.
- Do not power a complete display assembly from another board's logic rail.

## Information Still Needed

- Exact Waveshare SKU and hardware revision printed on each PCB.
- Official schematics and example-project version for each board.
- Available connector pins after enclosure wiring.
- Cable lengths and whether the boards share one regulated supply.
- Desired gauge assignment for center, left, and right at first boot.
