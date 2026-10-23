# Hondata CAN Reference

This reference was migrated from the read-only MackoDash `3.0.1` checkout at
commit `1678563`. MackoDash Triple supports Hondata only; its generic multi-ECU
JSON loader is not copied. The P4 owns CAN reception and decoding.

## Frames

All values are standard 11-bit CAN identifiers and multi-byte values are
big-endian. A frame is rejected when its DLC is too short for the listed data.

| ID | Bytes | Signal | Original conversion | Normalized storage |
|---:|---:|---|---|---|
| `0x660` | 0-1 | RPM | `raw` | RPM |
| `0x660` | 2-3 | Speed | `raw` | `raw * 10` in 0.1 kph |
| `0x660` | 4 | Gear | `raw` | gear number |
| `0x660` | 5 | Battery | `raw * 0.1` V | `raw * 100` mV |
| `0x661` | 0-1 | Intake air | `raw * 1.8 + 32` F | `raw * 10` in 0.1 C |
| `0x661` | 2-3 | Coolant | `raw * 1.8 + 32` F | `raw * 10` in 0.1 C |
| `0x662` | 0-1 | TPS | `raw` percent | `raw * 10` in 0.1 percent |
| `0x662` | 2-3 | MAP | `raw * 0.0145038 - 14.6959` psi (raw is 0.1 kPa absolute) | `raw - 1013` in 0.1 kPa gauge |
| `0x663` | 0-1 | Injector duration | `raw * 0.001` | raw retained; original unit was undocumented |
| `0x663` | 2-3 | Ignition angle | `raw` degrees | signed degrees |
| `0x664` | 2-3 | Lambda/AFR | `raw * 0.00044835` | AFR * 1000 |
| `0x665` | 0-1 | Knock count | `raw` | count |
| `0x666` | 0-1 | Cam target | `raw` degrees | signed degrees |
| `0x666` | 2-3 | Cam actual | `raw` degrees | signed degrees |
| `0x667` | 0-7 | Analog 0-3 | `raw * 0.00122` V | millivolts |
| `0x668` | 0-7 | Analog 4-7 | `raw * 0.00122` V | millivolts |
| `0x669` | 0 | Ethanol frequency | `raw` | Hz |
| `0x669` | 1 | Ethanol duty | `raw * 2.56` | 0.01 percent |
| `0x669` | 2 | Ethanol content | `raw` | 0.1 percent |

## UI Conversions

The original UI applied these conversions after CAN decoding:

- Speed: `mph = kph * 0.621371`; values below 3 mph displayed as zero.
- Temperature: CAN raw Celsius was displayed as `F = C * 1.8 + 32`.
- MAP: absolute kPa was converted to gauge pressure by subtracting standard
  atmosphere, then displayed in psi. Metric display multiplied psi by `6.89476`.
- Fuel level: one selectable Hondata analog input was calibrated with configurable
  empty/full millivolt endpoints, then clamped to 0-100 percent.
- TPS was rejected by the UI when outside 0-100 percent.
- Ignition timing was rejected by the UI when outside -100 to 100 degrees.

Injector duration and knock count were present in Hondata frames but were not
connected to the original UI data structure. MackoDash Triple retains both so
future injector-duty and knock displays can use live data after their exact
engineering units are confirmed.

## CAN Runtime Reference

The original firmware used ESP-IDF TWAI in normal mode, accepted standard frames,
ignored RTR and extended frames, and considered CAN data stale after 1.5 seconds.
When bitrate was not configured, it probed 1 Mbit/s, 500 kbit/s, 250 kbit/s, then
125 kbit/s. Bus-off recovery, stale-controller restart, receive-miss counters,
and error counters should be retained when the P4 TWAI acquisition task is added.

The original GPIO47/GPIO48 assignment belonged to a different P4 display board.
MackoDash Triple uses P4 GPIO4 for controller RX and GPIO5 for controller TX to
the external CAN transceiver.