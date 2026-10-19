# Hardware scope and wiring

I removed the factory handset from my desk to keep the setup clean. I control
all desk movement through Home Assistant using the ESPHome native API.
The ESP32 bridge talks to the existing SmartDesk motor control box over UART;
the motor control box still drives the lift motors.

The upstream project keeps the factory keypad alongside Home Assistant.
My setup has no factory handset. The ESP32 bridge has no built-in movement
buttons or display. Factory-keypad coexistence has not been validated with
this fork's motion engine.

## Assembled controller

<img src="../images/controller-enclosure.jpg" alt="My assembled desk controller and cable" width="360" />

This is my assembled ESP32 bridge, with the cable unplugged from the desk.
The photo shows the enclosure and plug. I haven't documented the internal
board layout, wire colors, solder connections, or connector pinout here.

## Cable used

I used this [cable (Amazon ASIN B0B1ZK35WW)](https://www.amazon.com/dp/B0B1ZK35WW)
for my controller. I've removed the tracking parameters from the purchase link.
I haven't verified its brand, original length, conductor mapping, or electrical
specifications. I haven't confirmed that the upstream cable's color map
applies to mine.

## Enclosure

I haven't identified the exact case model or print source. I've included these
upstream designs as references, but I haven't confirmed that they match mine:

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

## Control interface

I recommend setting up another way to reach your Home Assistant desk controls
before removing the handset: a dashboard, mobile app, separate control panel,
or Alexa connected through Home Assistant. These interfaces send commands
through Home Assistant to the ESP32; they aren't buttons on this enclosure.

There is no GPIO button mapping, screen driver, or independent local Stop
button in this firmware. My setup depends on Home Assistant and the network
for control, with no factory handset to fall back to.
