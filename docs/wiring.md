# Hardware scope and wiring

The replacement package talks to the existing SmartDesk motor control box;
it does not drive lift motors directly. It is intended for a setup where the
original handset is no longer connected. Factory-keypad coexistence has not
been validated with the replacement motion engine.

## Assembled controller

<img src="../images/controller-enclosure.jpg" alt="Assembled desk controller and its cable, supplied by the builder" width="360" />

This is the builder's assembled controller, photographed with the cable plug
outside the desk. The enclosure is closed, so the photo does not verify the
internal board layout, wire colors, solder connections, or connector pinout.

## Cable used

The builder identified [Amazon ASIN B0B1ZK35WW](https://www.amazon.com/dp/B0B1ZK35WW)
as the cable used for this assembly. The purchase link is recorded without
tracking parameters. Its product listing was unavailable during documentation;
brand, original length, conductor mapping, and electrical specifications remain
unverified. Do not substitute the upstream cable's color map for this cable.

## Enclosure

The exact case model or print source has not been identified. These upstream
design files are included as references, not as a confirmed match:

- [Base STL](../3d-printable-case/desky-esp32-base.stl), with a 60 × 43 × 24 mm bounding box.
- [Cover STL](../3d-printable-case/desky-esp32-cover.stl).
- [Fusion 360 source](../3d-printable-case/desky-esp32-box%20v5.f3d).

## Wiring and firmware target

The example uses an ESP32 `esp32dev`, UART TX GPIO17 and RX GPIO4 at 9600/8N1.
These match the tested firmware setup but must be checked against the actual
board and cable. Pin colors and voltages are not established by the YAML.
Confirm the controller pinout, logic levels, power requirements, and ground
before connecting a different desk. Do not infer them from another cable's
colors.

The original project's cable table, housing files, and keypad-tapping design
remain in [upstream-README.md](upstream-README.md). They describe a different
installation approach; the diode arrangement there does not establish that
two controller transmitters can safely share the new setup.

A dedicated button/display board is planned. There is no GPIO button mapping,
screen driver, or independent local Stop implementation in this version.
Keep hardware-specific firmware and validated wiring here when added; do not
represent the current API actions as completed physical-button support.
