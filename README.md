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
Air Ride, and Climate routing. The display nodes and rotary controller exchange
role-addressed, CRC-protected frames over the P4's Wi-Fi/UDP network. The center
remains the RPM tachometer unless covered by an overlay, the left renders
configurable CAN telemetry, and the right renders Air Ride with an optional
battery-voltage readout.

```text
                         CAN
                          |
                  +-------v-------+
                  |  Center P4    |
                  |  authority    |
                  +---+-------+---+
             Wi-Fi/UDP   |       |   Wi-Fi/UDP
                +--------v-+   +-v--------+
                | Left S3  |   | Right S3 |
                | display  |   | display  |
                +----------+   +----------+
                                 Wi-Fi/UDP
                                    |
                            +-----v-----+
                            | Knob S3   |
                            | controls  |
                            +-----------+
```

## Projects

- `apps/center_p4`: center controller and 800x800 display.
- `apps/left_s3`: left 480x480 display node.
- `apps/right_s3`: right 480x480 display node.
- [`apps/knob_s3`](apps/knob_s3/README.md): Elecrow rotary Wi-Fi/UDP control surface.
- `private/`: private submodule with network credentials, the Air Ride radio
  bridge, and the climate controller node (firmware, wiring and bench tests).
  The firmware builds need it checked out.
- `components/cluster_protocol`: versioned cluster frames with CRC and heartbeat.
- `components/cluster_wireless`: role-addressed Wi-Fi/UDP transport.
- `components/cluster_board_config`: shared board and timeout contract.
- `components/vehicle_data`: normalized cross-device vehicle state.
- `components/settings_schema`: shared display settings and left telemetry selections.
- `components/side_display`: Waveshare 2.8C panel bring-up and the speedometer gauge.
- `components/side_pages`: the five pages both side displays share (data, gauges,
  speedometer, Air Ride, climate) and their swipe navigation.
- `components/dash_fonts`: Microgramma digit fonts shared by the side displays.
- `ui-mockups/civic_tach`: retained display artwork, layout metadata, notes,
  previews, and editable Python generators.
- [`ui-mockups/knob`](ui-mockups/knob/KNOB_UI_HANDOFF.md): retained rotary UI
        artwork, layout metadata, previews, and editable generator.

The original MackoDash repositories remain unchanged and are reference sources,
not submodules or build dependencies.
