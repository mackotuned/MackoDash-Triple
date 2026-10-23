# Hondata SD Logging

The center P4 mounts its onboard microSD card and stores sessions under
`/MDLOGS/logNNNN.bin`. The Knob `REC` control starts and stops recording.
The existing `logging.autoRecord` setting starts a session at boot when a card
is available. Center reports the authoritative SD and recording state to the
Knob after every request and once per heartbeat, so a failed request cannot
leave the Knob showing a false active state.

Each 10 Hz record contains every decoded Hondata value: frame-validity mask,
per-frame timestamps, RPM, speed, gear, battery, intake and coolant temperature,
throttle, MAP, injector duration, ignition angle, AFR, knock count, target and
actual cam angle, all eight analog inputs, and all three ethanol values. File
headers and records are versioned, fixed-width, and protected by CRC-32.

At most 32 sessions are retained. Starting session 33 removes session 1, and so
on. A one-hour session is approximately 3.6 MB.

To validate a log and convert it to CSV:

```powershell
python tools/export_hondata_log.py E:\MDLOGS\log0001.bin
```

The Center `HONDATA LOG` view shows the latest recorded sample, all decoded
values, session number, sample count, and whether recording is active. Recording
is disabled when the SD card cannot be mounted; the rest of the cluster remains
operational.