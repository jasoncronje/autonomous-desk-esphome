# Motion controls and protocol

## Native API and UI

I use Home Assistant as the control interface for every desk movement. I
removed the factory handset, so I use an external interface to reach these
controls. That can be a dashboard, the mobile app, a separate control panel,
or Alexa configured through Home Assistant. Voice-assistant integration is
configured in Home Assistant, not in this firmware.

Motion Enabled must be ON before a command. Stop disarms it. A native API
client disconnect also disarms it, even if another client remains connected.
Reconnect never resumes an old command. My ESP32 bridge has no local movement
or Stop buttons; Home Assistant and network access are needed for normal control.

Existing raw height, inches readback, nudge, and preset controls keep their
ESPHome names. Added controls are Height (inches), Motion Enabled, Stop,
Move Up/Down, Moving, and Motion Status. Entity/action prefixes depend on the
ESPHome device name; inspect the actions registered by Home Assistant.

For a device named `autonomous-desk`, hold actions have these names:

| Event | Action | Data |
| --- | --- | --- |
| Press | `esphome.autonomous_desk_desk_hold_start` | `direction: 1` or `-1`, positive integer `token` |
| Every 250 ms while held | `esphome.autonomous_desk_desk_hold_renew` | Same `token` |
| Release/cancel | `esphome.autonomous_desk_desk_hold_release` | Same `token` |

Use a new token for each press. Begin renewal only after start succeeds; never
queue renewals while a request is pending. Stop renewal before release. Send
release on pointer-up/cancel, lost capture, blur, and hidden-page events.
Renewal expires after 750 ms; a late renewal cannot restart a stopped hold.
The ordinary Move Up/Down HA buttons have no release event and make one short
leased move. They are not a complete press-and-hold UI.

Moving reflects recent accepted height changes with a 750 ms decay. It is not
a motor-current signal and can remain ON briefly after physical stopping.
A replacement operation cancels the old one and waits 750 ms before starting.
Stop also cancels a pending replacement. One controller owns UART output.

## Preset startup

The desk can briefly report a distant height during preset startup, then
return to its current position before travel begins. Treating that flash as
real progress caused the old one-second completion check to stop keepalives
too early. The meaning of RX status 0x03 is not established; it is not assumed
to identify a preset target.

During a native preset, reject changes exceeding two raw steps plus ten steps
per second since the last accepted feedback, capped at the one-second feedback
window. Rejected frames update neither observed height nor watchdog progress.
Observed travel was roughly 2-5 raw steps/s; the margin allows quantization
and batching. Two consecutive changes in one direction after sending the
preset key confirm travel. Single-step bouncing cannot confirm startup.

Startup is limited to 3 s after the key, then reports `Preset travel unconfirmed`.
After confirmed travel, 3 s without another height change ends keepalives and
reports `Preset stationary`. The stored target is not known to this firmware,
so this status is observation, not proof of arrival. Very short moves or an
already-reached preset may finish without confirmed travel.

## Protocol and limits

UART: 9600 baud, 8N1. TX: `D8 D8 66 key key`; up=2, down=1,
neutral=0, preset keys=4/8/16/32, save=64. RX has six bytes:
`98 98 status status height height`, accepting status 0 or 3, matching
pairs and configured height bounds. The parser accepts fragmented frames.

Direction packets repeat every 50 ms. Native presets send the preset key,
then neutral keepalives. Preset/save cancellation stops transmission; manual
motion cancellation optionally emits one neutral release then silence. A
neutral release is experimental physical stopping behavior, not a guaranteed
motor brake. See [protocol research](https://github.com/Stefichen5/AutonomousControl#protocol).

Feedback older than 1 s cancels motion. Manual stalled progress cancels after
3 s. Total motion is bounded to 30 s. Targets stop at a crossing without
reversing to chase an exact value. Saving retains the original 68 save-key
packets then a preset key, but the operation is cancellable.

Only compile/simulate by default. Physical movement, Stop latency, memory
writes, and any wiring changes need supervised validation on the actual desk.
