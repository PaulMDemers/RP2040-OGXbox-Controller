# RP2040 OG Xbox Controller

Arduino firmware for using a Waveshare RP2040-Zero as an original Xbox XID gamepad, plus a second-RP2040 UART bridge for sending live control commands while the controller board's native USB port is connected to the Xbox. The repo also includes an experimental one-chip Pico SDK firmware that moves the Xbox-facing USB connection to GPIO with Pico-PIO-USB.

This is an early but working native-USB build. The Xbox-facing RP2040 enumerates as a Duke-style controller (`045E:0202`) and answers the Xbox-specific XID descriptor probes used during controller startup.

## What is Included

- `firmware/RP2040_OGXbox_Controller`: Xbox-facing controller firmware.
- `firmware/RP2040_UART_Bridge`: USB-serial to UART bridge firmware for a second RP2040 board.
- `firmware/RP2040_OGXbox_Controller_GPIO_PIO`: experimental one-chip GPIO USB firmware.
- `tools/xidctl.ps1`: Windows PowerShell helper for sending controller commands through the bridge.
- `diagnostics/xid_usb_diag`: optional NXDK homebrew diagnostic XBE source with UDP broadcast logging.
- `docs/wiring.md`: wiring notes for the Xbox cable and bridge board.

## Current Hardware Path

The current validated setup uses the RP2040-Zero native USB port as the Xbox controller connection. The Xbox powers the controller board through the controller cable, and the RP2040 USB D+/D- pins connect to the Xbox cable USB data pair.

Because that USB port is occupied by the Xbox, live command input uses a second RP2040 board as a USB-serial bridge:

```text
PC USB -> bridge RP2040 -> UART -> Xbox-facing RP2040 -> native USB -> Xbox
```

The experimental one-chip path moves the Xbox-facing USB connection to GPIO using PIO USB so the main RP2040 USB-C port can stay available for serial control:

```text
PC USB serial/control -> RP2040 native USB-C
Xbox controller port -> RP2040 GPIO PIO USB
```

## Build Requirements

- Arduino IDE or Arduino CLI.
- Earle Philhower's Arduino-Pico board package.
- Adafruit TinyUSB support from that board package.
- Waveshare RP2040-Zero or compatible RP2040 board.
- Optional: nxdk for building the Xbox USB diagnostic.
- Optional for the experimental GPIO firmware: CMake, make, and the Pico SDK included with Arduino-Pico.

Install/update the RP2040 board package with:

```powershell
& 'C:\Program Files\Arduino CLI\arduino-cli.exe' core update-index --additional-urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
& 'C:\Program Files\Arduino CLI\arduino-cli.exe' core install rp2040:rp2040 --additional-urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
```

## Build the Controller Firmware

Build both Arduino sketches:

```powershell
.\scripts\build_arduino.ps1
```

Build both sketches with the controller idle Down/Up startup loop disabled:

```powershell
.\scripts\build_arduino.ps1 -DisableIdleScript
```

Or build the controller firmware directly:

```powershell
& 'C:\Program Files\Arduino CLI\arduino-cli.exe' compile --clean --export-binaries --fqbn rp2040:rp2040:waveshare_rp2040_zero:usbstack=tinyusb --additional-urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json .\firmware\RP2040_OGXbox_Controller
```

Direct controller build with the idle loop disabled:

```powershell
& 'C:\Program Files\Arduino CLI\arduino-cli.exe' compile --clean --export-binaries --fqbn rp2040:rp2040:waveshare_rp2040_zero:usbstack=tinyusb --build-property "compiler.cpp.extra_flags=-DOGXB_ENABLE_IDLE_SCRIPT=0" --additional-urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json .\firmware\RP2040_OGXbox_Controller
```

Flash the generated UF2 from:

```text
firmware/RP2040_OGXbox_Controller/build/rp2040.rp2040.waveshare_rp2040_zero/RP2040_OGXbox_Controller.ino.uf2
```

The `usbstack=tinyusb` option is required. The firmware disables Arduino's auto CDC serial interface before registering the XID interface so the Xbox sees a single-interface controller.

## Build the UART Bridge

```powershell
& 'C:\Program Files\Arduino CLI\arduino-cli.exe' compile --export-binaries --fqbn rp2040:rp2040:waveshare_rp2040_zero --additional-urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json .\firmware\RP2040_UART_Bridge
```

Flash the generated UF2 from:

```text
firmware/RP2040_UART_Bridge/build/rp2040.rp2040.waveshare_rp2040_zero/RP2040_UART_Bridge.ino.uf2
```

## Build the Experimental GPIO PIO Firmware

The one-chip firmware is a Pico SDK project, not an Arduino sketch:

```powershell
.\scripts\build_gpio_pio.ps1
```

Flash the generated UF2 from:

```text
build/gpio_pio/rp2040_ogxbox_controller_gpio_pio.uf2
```

Default GPIO USB pins are `GP2` for Xbox D+ and `GP3` for Xbox D-. See `docs/gpio-pio-usb.md` before wiring, especially the pull-up resistor and 5V isolation notes.

## Wiring

See `docs/wiring.md` for the full wiring notes. In short:

Xbox cable to controller RP2040:

| Xbox cable | RP2040-Zero |
| ---------- | ----------- |
| Red / 5V | 5V |
| Black / GND | GND |
| USB D+ | USB D+ |
| USB D- | USB D- |
| Yellow | Not connected |

Bridge RP2040 to controller RP2040:

| Bridge RP2040 | Controller RP2040 |
| ------------- | ----------------- |
| GND | GND |
| GP0 / UART TX | GP1 / UART RX |
| GP1 / UART RX | GP0 / UART TX |

Verify D+ and D- before powering the cable. Some published color tables disagree.

For the experimental one-chip GPIO path, see `docs/gpio-pio-usb.md`.

## Startup Behavior

On boot, before any UART command is received, the controller sends a repeating test input:

- Down
- Up

Any UART control command disables the startup script. Send `SCRIPT ON` to re-enable it. If the firmware was built with `OGXB_ENABLE_IDLE_SCRIPT=0`, the controller boots neutral and `SCRIPT ON` returns an error.

## Send Commands

List serial ports:

```powershell
.\tools\xidctl.ps1 -ListPorts
```

Run the simple Down/Up/Clear demo:

```powershell
.\tools\xidctl.ps1 -Port COM7 -Demo
```

Send specific commands:

```powershell
.\tools\xidctl.ps1 -Port COM7 -Command "DOWN 150","UP 150","CLEAR"
```

Auto-detect the controller serial port and send a status request:

```powershell
.\tools\xidctl.ps1 -Auto -Status
```

Run a command script:

```powershell
.\tools\xidctl.ps1 -Auto -ScriptPath .\scripts\example_controls.xid
```

Use convenience flags:

```powershell
.\tools\xidctl.ps1 -Auto -Tap UP -TapMs 120
.\tools\xidctl.ps1 -Auto -Hold DOWN
.\tools\xidctl.ps1 -Auto -Release DOWN
.\tools\xidctl.ps1 -Auto -Axis LX -AxisValue -22000
.\tools\xidctl.ps1 -Auto -Clear
```

Interactive mode:

```powershell
.\tools\xidctl.ps1 -Port COM7 -Interactive
```

Supported newline-delimited commands include:

```text
PING
HELP
STATUS
DOWN 150
UP 150
TAP A 120
HOLD B
RELEASE B
AXIS LX -22000
AXIS LX 0
TRIG R 255
TRIG R 0
CLEAR
SCRIPT ON
SCRIPT OFF
```

Short forms like `A 100`, `B 100`, `UP 100`, and `LEFT 100` are treated as taps.

## Optional Xbox Diagnostic

The diagnostic in `diagnostics/xid_usb_diag` is an nxdk homebrew app that prints USB/XID status on screen and broadcasts status lines over UDP to port `49036`.

Build with MSYS2 and nxdk:

```powershell
.\scripts\build_diag_msys.ps1 -NxdkDir C:\path\to\nxdk
```

You can also set `NXDK_DIR` and run `.\scripts\build_diag_msys.ps1`.

Or build from the diagnostic folder with nxdk available:

```powershell
make NXDK_DIR=C:\path\to\nxdk
```

Listen for broadcasts on the PC:

```powershell
.\diagnostics\xid_usb_diag\udp_diag_listener.ps1
```

Upload the built XBE to an Xbox over FTP:

```powershell
.\scripts\upload_xbe.ps1 -HostName 192.168.50.156 -User xbox -Password xbox -RemoteDir /E/Applications/xib_diag
```

This was useful for confirming that the controller enumerated as `045E:0202` and that the Xbox XID stack was binding to the expected interface.

## Notes

- The controller firmware accepts rumble output and lights the board LED while either motor value is non-zero.
- Do not power the controller RP2040 from the PC and Xbox at the same time.
- Do not connect 5V Arduino TX directly to an RP2040 RX pin. Use 3.3V UART logic.
- This project is not affiliated with Microsoft.
