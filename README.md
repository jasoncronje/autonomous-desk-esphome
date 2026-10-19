# Autonomous Desk Control for Home Assistant

This is my **Home Assistant-first** fork of
[at-karan-sharma/autonomous-desk-esphome](https://github.com/at-karan-sharma/autonomous-desk-esphome).
I removed the factory handset from my desk to keep the setup clean. I don't
have a physical desk controller: I send all movement, height, preset, and Stop
commands through Home Assistant using the ESPHome native API.

The original project adds Home Assistant control while retaining the factory
keypad. My setup makes Home Assistant the main control interface and removes
the handset. That's the core difference behind this fork.

The ESP32 in the photo is the bridge between Home Assistant and the desk's
existing motor control box. It sends UART commands; the motor control box
still drives the lift motors. The bridge has no built-in movement buttons or
display.

## Control setup

If you use this setup, I recommend having another control interface ready
before removing the handset. That could be:

- A Home Assistant dashboard or the mobile app.
- A separate control panel or remote that sends commands through Home Assistant.
- Alexa or another voice assistant configured to call your Home Assistant controls or scripts.

Voice control needs to be configured in Home Assistant; it isn't built into
this firmware. Make movement and Stop controls easy to reach from whichever
interface you choose. With the handset removed, my setup has no physical
fallback if Home Assistant or the network is unavailable.

The original keypad-preserving implementation remains at `config/desky.yml`;
its documentation is preserved in [docs/upstream-README.md](docs/upstream-README.md).
The replacement engine is a separate package: `config/controller.yaml`.
Do not load both packages on the same device.

## Features

- Continuous numeric target moves at a 50 ms packet cadence, without repeated nudges.
- Raw height plus direct inches input and observed-height readback.
- Sessioned hold start/renew/release, with a 750 ms lease if renewal stops.
- Nudge, four native preset recalls, and cancellable preset saving.
- Stop cancels the operation and disables motion; re-enable before another move.
- Motion disabled at boot and after any native API client disconnect.
- Feedback loss, stalled movement, wrong direction, travel limits, and a total movement timeout cancel operations.
- Preset startup filters implausible height jumps and requires sustained travel before checking stationarity.

## Hardware used

<img src="images/controller-enclosure.jpg" alt="Assembled desk controller in a black enclosure with a short cable and modular plug" width="360" />

This is my ESP32 bridge in its enclosure. I used this
[cable (Amazon ASIN B0B1ZK35WW)](https://www.amazon.com/dp/B0B1ZK35WW).
I haven't verified the listing's brand, length, or pinout. The photo shows the
outside of my controller, not its internal wiring.

I haven't identified the exact case model. I've kept the upstream
[3D-printable case files](3d-printable-case/) as references, but I haven't
confirmed that they match my enclosure.
See [hardware and wiring details](docs/wiring.md).

## Installation

Tested with ESPHome **2026.7.2**, ESP32 `esp32dev`, and **ESP-IDF**.
The replacement package defaults to raw height **75-123** (about 29.5-48.4 in).
The inches input range currently targets that regular-height desk; extended
models need separate validation and matching input bounds.

Use [example.yml](example.yml) as the device configuration. It loads both the
YAML package and the C++ engine through an ESPHome external component; no
manual header copying is required. Put personal values in your local
`secrets.yaml` using these keys:

- `wifi_ssid`
- `wifi_password`
- `desk_api_encryption_key` (a valid ESPHome native API encryption key)
- `desk_ota_password`

Set UART pins and raw travel limits for the actual desk before compiling.
Pins in the example are examples, not a universal wiring map. For deployment,
pin **both** GitHub references in `example.yml` to the same reviewed commit
instead of tracking `main`.

```sh
python3 -m pip install -r requirements.txt
esphome config example.yml
esphome compile example.yml
```

Compilation does not install firmware. Uploading restarts the ESP32. The desk
starts with Motion Enabled OFF; enabling it does not itself command movement.
Keep existing device names and IDs when migrating an existing installation.
See [docs/wiring.md](docs/wiring.md) and [docs/controls.md](docs/controls.md).

## Stopping and validation limits

Firmware cancellation is tested separately from physical motor braking.
Manual/target/nudge cancellation sends one neutral release packet then goes
silent. Native preset/save cancellation only goes silent, because neutral
packets also serve as preset keepalives. There is no verified immediate brake
command for every mode. Earlier silence-only firmware continued reporting
movement for about a second after Stop; the current release packet is not a
proven immediate physical brake. Supervise movement tests.

Preset travel is confirmed by two consecutive height changes in the same
direction. Startup expires after 3 s; confirmed travel ends after 3 s without
another height change. `Preset stationary` does not prove the stored target
was reached. `Preset travel unconfirmed` can also occur when already at that
preset or after a very short move. See the protocol and validation notes in
[docs/controls.md](docs/controls.md).

The native controller stores presets. Selecting the memory-save control
**overwrites that slot**; keep it out of the ordinary movement UI.

## Development

```sh
make test       # 30 simulated scenarios; no device connection
make compile    # ESP32 build with dummy credentials; never upload this binary
```

Host tests use AddressSanitizer and UndefinedBehaviorSanitizer. Leak checking
is disabled for environments where LeakSanitizer cannot run under ptrace.
The compile harness is only for build validation and contains dummy network
and API credentials. Do not use `esphome run` or `upload` with it.

Files:

- `components/desk_motion/desk_motion.h`: standalone motion engine.
- `components/desk_motion/__init__.py`: ESPHome header packaging.
- `config/controller.yaml`: UART and native API controls.
- `tests/`: simulated UART scenarios and compile-only harness.
- `docs/`: controls, protocol constraints, wiring, and original documentation.

Local device credentials, firmware binaries, and deployment records are not
part of this repository. Personal dashboards, voice-assistant setup, and other
Home Assistant control interfaces are configured separately.

## Attribution

The upstream MIT license and history are retained. Karan Sharma's original
ESPHome implementation provided the conversion formula and protocol sequences.
Protocol research credits:
[Stefichen5/AutonomousControl](https://github.com/Stefichen5/AutonomousControl),
[developit's desk driver](https://gist.github.com/developit/d610e45a522810b5287db61e554ae9c9),
and the contributors credited by the original project.

This fork replaces the original motion scheduling logic. Its revision 3 preset
startup fix corrects a regression in this fork, not a defect attributed to the
original project. Small compatible improvements can be proposed upstream
separately; this repository focuses on Home Assistant-first control with the
factory handset removed.
