# RP2040 OG Xbox Controller Firmware

This sketch makes a Waveshare RP2040-Zero enumerate as an original Xbox XID gamepad.

Build with the Arduino-Pico TinyUSB stack:

```powershell
& 'C:\Program Files\Arduino CLI\arduino-cli.exe' compile --clean --export-binaries --fqbn rp2040:rp2040:waveshare_rp2040_zero:usbstack=tinyusb --additional-urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json .\firmware\RP2040_OGXbox_Controller
```

The startup script sends Down/Up until a UART command is received. Disable that at build time with:

```powershell
.\scripts\build_arduino.ps1 -DisableIdleScript
```

UART command input uses `Serial1` at `115200` baud, with GP1 as RX and GP0 as TX.
