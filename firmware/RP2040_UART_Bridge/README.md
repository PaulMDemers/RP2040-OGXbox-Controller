# RP2040 UART Bridge

Temporary USB-serial bridge for controlling the Xbox-facing RP2040-Zero while its native USB port is connected to the Xbox.

Build with:

```powershell
& 'C:\Program Files\Arduino CLI\arduino-cli.exe' compile --export-binaries --fqbn rp2040:rp2040:waveshare_rp2040_zero --additional-urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json .\firmware\RP2040_UART_Bridge
```

Wiring:

| Bridge board | Controller RP2040-Zero |
| ------------ | ---------------------- |
| GND | GND |
| GP0 / UART TX | GP1 / UART RX |
| GP1 / UART RX | GP0 / UART TX |

Do not connect 5V Arduino TX directly to the RP2040 RX pin.
