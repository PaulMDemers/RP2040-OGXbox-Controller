# Wiring

## Xbox Cable to Controller RP2040

The original Xbox controller port is USB plus an extra yellow light-gun/VBlank wire. The current firmware uses the RP2040-Zero native USB pins for the Xbox-facing controller connection.

| Xbox cable | RP2040-Zero |
| ---------- | ----------- |
| Red / 5V | 5V |
| Black / GND | GND |
| USB D+ | USB D+ |
| USB D- | USB D- |
| Yellow | Not connected |

Verify D+ and D- before powering the console-side cable. Some published color tables disagree, and some extension cables do not follow the same internal colors.

If Windows sees the adapter but the Xbox does not respond, first try swapping D+ and D- on the Xbox cable side. Also confirm the Xbox is powering the RP2040-Zero from the console cable and that the RP2040-Zero is not simultaneously powered from the PC USB port.

## UART Bridge

Use a second 3.3V RP2040 board as the USB-serial bridge when the controller RP2040 native USB port is plugged into the Xbox.

| Bridge board | Controller RP2040-Zero |
| ------------ | ---------------------- |
| GND | GND |
| GP0 / UART TX | GP1 / UART RX |
| GP1 / UART RX | GP0 / UART TX |

The controller firmware listens on `Serial1` at `115200` baud.

Do not connect 5V Arduino TX directly to the RP2040 RX pin.

## Validated Flow

```text
PC USB
  |
  v
Bridge RP2040 USB serial
  |
  | UART at 115200 baud
  v
Controller RP2040
  |
  | Native USB wired into Xbox controller cable
  v
Original Xbox controller port
```

## Future GPIO USB Path

The next hardware revision should move the Xbox-facing USB connection to RP2040 GPIO using PIO USB. That would leave the controller RP2040 native USB-C port available for serial control and firmware flashing without a second bridge board.
