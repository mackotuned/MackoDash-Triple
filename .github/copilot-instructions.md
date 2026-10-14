# MackoDash Triple Instructions

- Use ESP-IDF 5.5.3 and C unless an existing component requires C++.
- Keep `apps/center_p4`, `apps/left_s3`, and `apps/right_s3` independently buildable.
- Put cross-device contracts in `components`; never include source directly from another app.
- The P4 is authoritative for CAN, settings, warnings, logging, Air Ride, and Climate.
- The S3 display nodes render received state and must enter an obvious offline state after heartbeat loss.
- Use explicit-width integer wire fields, protocol versions, bounds checks, sequence numbers, and CRC validation.
- Do not modify the original MackoDash repositories from this workspace.
- Add display BSPs only after confirming the exact Waveshare board revision and official pin definitions.
- Build all affected targets after shared protocol or schema changes.
- Never flash a target until its connected serial port and board identity are confirmed.
