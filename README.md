# MackoDash Triple

MackoDash Triple is a three-screen gauge cluster built from one authoritative
ESP32-P4 center controller and two ESP32-S3 side displays.

## Hardware

- Center: Waveshare ESP32-P4 3.4-inch round 800x800 display.
- Left: Waveshare ESP32-S3 2.8-inch round 480x480 display.
- Right: Waveshare ESP32-S3 2.8-inch round 480x480 display.
- Existing MackoDash S3 rotary controller and ESP32-WROOM HVAC node.

## Architecture

The P4 owns CAN decoding, vehicle state, settings, warnings, logging, odometer,
Air Ride, and Climate routing. Each display S3 receives normalized state from a
dedicated UART and only renders its assigned content. The existing rotary
controller remains a separate control surface connected to the P4.

```text
                         CAN
                          |
                  +-------v-------+
                  |  Center P4    |
                  |  authority    |
                  +---+-------+---+
              UART-L |       | UART-R
            +--------v-+   +-v--------+
            | Left S3  |   | Right S3 |
            | display  |   | display  |
            +----------+   +----------+
```

## Projects

- `apps/center_p4`: center controller and 800x800 display.
- `apps/left_s3`: left 480x480 display node.
- `apps/right_s3`: right 480x480 display node.
- `components/cluster_protocol`: framed UART protocol with CRC and heartbeat.
- `components/vehicle_data`: normalized cross-device vehicle state.
- `components/settings_schema`: shared screen and gauge assignments.

## First Milestone

1. Build all three hardware-neutral firmware shells.
2. Confirm a heartbeat and test vehicle state over each dedicated UART.
3. Integrate the official Waveshare board support packages.
4. Render one smooth analog gauge on each screen.
5. Port proven CAN, settings, Air Ride, and Climate modules from MackoDash.

The original MackoDash repositories remain unchanged and are reference sources,
not submodules or build dependencies.
