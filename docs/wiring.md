# Hardware scope and wiring

The replacement package talks to the existing SmartDesk motor control box;
it does not drive lift motors directly. It is intended for a setup where the
original handset is no longer connected. Factory-keypad coexistence has not
been validated with the replacement motion engine.

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
