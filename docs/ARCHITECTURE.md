# Architecture

## Ownership

The center P4 is the single source of truth. It acquires raw CAN traffic,
decodes ECU protocols, applies smoothing and unit conversion, stores settings,
handles warnings and logs, and routes control commands. Side displays do not
connect to CAN and do not persist authoritative settings.

The right S3 owns the Air Ride radio bridge (a private component). It may
transmit a local hold command only while both P4 heartbeat and Air Ride
telemetry are live. Loss of either link disarms controls and releases the valves.

Both side S3 displays run the same eight pages from `components/side_pages`:
Data, Gauges, Trends, Peaks, Timer, Speedometer, Air Ride and Climate. Swiping
cycles through them in that order, and the knob can put any of them on either
display. Data shows three values, Gauges a 2x3 grid and Trends two values
graphed over 20 seconds; each display has its own channel choices, set from the
phone settings page. Peaks holds session maxima and Timer measures 0-30, 0-60,
1/8 and 1/4 mile from the P4's sample timestamps; hold either screen to reset.
Data-driven pages keep tracking while off screen. The right S3 drives the Air
Ride radio directly; the left S3 shows the same readings relayed by the P4 and
sends its touch commands through the P4, which applies the same checks as phone
commands.

The left S3 is the climate gateway. The knob sends fan, mode and temperature
changes to the P4, which keeps the target dial positions and forwards them to
the left S3; the left S3 drives the climate controller node (private) and
reports its positions back through the P4 to the knob. The phone climate page
uses `GET /api/climate/state` and `POST /api/climate/command` on the P4. The P4 resends the
targets every second so a restarted node recovers them. Until the knob changes
something, the P4 adopts the positions the node reports.

## Links

The P4 hosts the WPA2 `MackoDash` SoftAP on Wi-Fi channel 6. Both S3 displays and
the rotary S3 join it as stations and exchange role-addressed UDP datagrams on
port 45500. Stations disable Wi-Fi power save so broadcast frames arrive
immediately instead of in bursts at each DTIM beacon.
The datagrams carry the existing versioned cluster frames:

- A periodic heartbeat.
- Fast vehicle state at 25 Hz. Displays smooth needles between samples at
  their ~60 fps frame rate (`components/vehicle_data/include/gauge_filter.h`).
- Slow status and warning state at 5-10 Hz.
- Left telemetry selections, right battery visibility, and settings revisions on change.

A node must show an obvious offline state after 2500 ms without a valid heartbeat.
Invalid length, version, or CRC frames are dropped.
The wireless envelope also rejects unknown source roles or packets not addressed
to the receiving role. The right S3 returns pressure, compressor, and active
adjustment state at 10 Hz; the P4 sends compressor commands only to the right role
and relays Air Ride state to the knob. Knob commands use fixed-width, versioned
payloads and are limited to 10 Hz while an adjustment is live. Confirmed brightness
changes are persisted by the P4; transient live updates are not written to NVS.
The P4 sends the knob a versioned logging status after each record request and
once per heartbeat, so the REC state always reflects SD availability and the
logger's actual state.
The right radio stays on channel 6 for both the cluster link and the Air Ride
bridge, so they share one radio without an external signal wire.

## Side Firmware Updates

Each S3 requires one initial USB flash to install the display application and an
OTA-capable receiver. A later milestone can embed the left and right S3 images in
the P4 firmware and stream them over the bidirectional wireless cluster network.
Reliable automatic updates must add block acknowledgements, retries, image
authentication, and rollback confirmation to the wireless transport.

## Rendering

The available display views are explicit:

- The center screen renders the primary RPM tachometer, the Air Ride pressure
	view, or the complete Hondata log view.
- The left screen renders CAN telemetry, the speedometer, or the Climate preview.
  Its main, lower-left, and lower-right
	numeric channels are configurable, while the fuel and coolant arcs are fixed.
- The right screen renders Air Ride, the speedometer, or the Climate preview.

The P4 stores the authoritative selections and sends them to the display nodes.
Rendering remains local so a delayed side display cannot block CAN processing or
the center UI. The retained visual source package, coordinate metadata, and
generator scripts are in [`ui-mockups/civic_tach`](ui-mockups/civic_tach/).

The knob renders the retained 240x240 UI package in
[`ui-mockups/knob`](ui-mockups/knob/) and enters an offline state after heartbeat
loss. Its screen selector only presents valid target-specific choices: Left
Speedometer/Data/Climate, Center Tach/Air/Logs, and Right
Speedometer/Air/Climate. Opening Climate from the main knob menu selects the
Right Climate view by default. Climate is visual-only until the HVAC authority
and vehicle integration are implemented. Unsupported view/display combinations
are rejected by the P4.

Opening Air Ride from the knob saves the current Center and Right views, then
opens Air Ride on both displays before adjustment begins. Back, Home, or idle
timeout sends the Air Ride release command before restoring both saved views.
The release remains mandatory on every adjustment exit path.

The right display overlays live tank and corner pressures on the retained Air
Ride face. Tapping a corner or the tank opens Claude's retained control surface;
release and press-loss events always transmit the safety release sequence.
Horizontal swipes cycle through each corner and the all-corners target, releasing
any active command before the target changes. The adjustment overlay leaves live
pressure display to the center. A two-second Air Out hold replaces preset buttons
and is rejected while either link is offline.

While a right-side adjustment is open, the P4 covers the tach with an Air Ride
overlay showing all four corner pressures, tank pressure, link state, and a
compressor toggle. The overlay hides when adjustment closes or return state is
stale. Compressor thresholds remain authoritative P4 settings; the right S3
rejects received commands unless both P4 heartbeat and Air Ride telemetry are live.

The P4 phone controller exposes live pressure, compressor, and dead-man manual
movement endpoints. The integration contract is kept in the private repository.

The Center records every decoded Hondata field at 10 Hz to the onboard microSD
card. The versioned binary format, retention behavior, and CSV exporter are
documented in [`HONDATA_LOGGING.md`](HONDATA_LOGGING.md).

## Migration

Proven MackoDash modules will be ported behind new boundaries rather than copied
wholesale. UI code from the 1024x600 dashboard is reference-only because all three
new displays are round and use different resolutions and controllers.

Hondata frame definitions and conversions are captured in `HONDATA_CAN.md` and
implemented by the `hondata_can` component. The decoder is independent of TWAI
GPIO selection so the original board's CAN pins are never assumed on the 3.4C P4.
