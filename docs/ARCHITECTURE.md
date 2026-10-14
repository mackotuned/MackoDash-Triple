# Architecture

## Ownership

The center P4 is the single source of truth. It acquires raw CAN traffic,
decodes ECU protocols, applies smoothing and unit conversion, stores settings,
handles warnings and logs, and routes control commands. Side displays do not
connect to CAN and do not persist authoritative settings.

## Links

The P4 uses one dedicated full-duplex UART per side display. Each link carries:

- A periodic heartbeat.
- Fast vehicle state at a target 50 Hz.
- Slow status and warning state at 5-10 Hz.
- Screen assignments and settings revisions on change.
- Display health and acknowledgement messages back to the P4.

A side display must mark data stale after 250 ms and show an offline state after
1000 ms without a valid frame. Invalid length, version, or CRC frames are dropped.

## Rendering

The center screen owns the primary speed/RPM experience and global warnings. The
left and right screens receive assignable gauge roles. Rendering remains local so
a delayed side display cannot block CAN processing or the center UI.

## Migration

Proven MackoDash modules will be ported behind new boundaries rather than copied
wholesale. UI code from the 1024x600 dashboard is reference-only because all three
new displays are round and use different resolutions and controllers.
