# Architecture

## Ownership

The center P4 is the single source of truth. It acquires raw CAN traffic,
decodes ECU protocols, applies smoothing and unit conversion, stores settings,
handles warnings and logs, and routes control commands. Side displays do not
connect to CAN and do not persist authoritative settings.

## Links

The P4 uses one dedicated simplex UART transmitter per side display. Both links
carry the same authoritative state from P4 TX to S3 RX:

- A periodic heartbeat.
- Fast vehicle state at a target 50 Hz.
- Slow status and warning state at 5-10 Hz.
- Screen assignments and settings revisions on change.

A side display must mark data stale after 250 ms and show an offline state after
1000 ms without a valid frame. Invalid length, version, or CRC frames are dropped.
There is no S3-to-P4 return wire, so acknowledgements and display-health telemetry
are not part of this transport. Sequence gaps and local receive timeouts provide
the side-display diagnostics.

## Side Firmware Updates

Each S3 requires one initial USB flash to install the display application and an
OTA-capable receiver. A later milestone can embed the left and right S3 images in
the P4 firmware and stream them into each S3 OTA partition over the cluster links.
Reliable automatic updates should add each S3 `TXD` (`GPIO43`) to a separate P4
RX pin so the P4 can receive block acknowledgements, retry failed blocks, and
confirm the new image before the S3 reboots. The current one-way wiring remains
sufficient for normal gauge data.

## Rendering

The center screen owns the primary speed/RPM experience and global warnings. The
left and right screens receive assignable gauge roles. Rendering remains local so
a delayed side display cannot block CAN processing or the center UI.

## Migration

Proven MackoDash modules will be ported behind new boundaries rather than copied
wholesale. UI code from the 1024x600 dashboard is reference-only because all three
new displays are round and use different resolutions and controllers.
