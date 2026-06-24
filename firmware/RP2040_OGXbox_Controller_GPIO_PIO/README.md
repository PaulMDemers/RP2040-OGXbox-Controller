# RP2040 OG Xbox Controller GPIO PIO Firmware

This is the experimental one-chip firmware. It keeps the RP2040 native USB port available as a USB CDC serial control channel for the PC, and uses Pico-PIO-USB on GPIO pins for the Xbox-facing XID controller device.

Default GPIO USB pins:

- `GP2`: Xbox D+
- `GP3`: Xbox D-

Build-time options live in `CMakeLists.txt`:

- `OGXB_PIO_USB_DP_PIN=2`: D+ pin. D- is the adjacent GPIO.
- `OGXB_ENABLE_IDLE_SCRIPT=0`: disables the old automatic up/down loop by default.

Hardware notes:

- Add a 1.5k pull-up from Xbox D+ to 3.3V. Pico-PIO-USB device mode does not provide this internally.
- Add 22 ohm series resistors on D+ and D- when laying out the cleaned-up hardware.
- Connect Xbox cable ground to RP2040 ground.
- Do not directly tie PC USB 5V and Xbox 5V together. For development, power the RP2040 from PC USB and leave the Xbox red 5V wire disconnected, or add power isolation before feeding both.
- The Xbox yellow wire is not used for this USB controller path.
