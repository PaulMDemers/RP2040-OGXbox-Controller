# GPIO PIO USB One-Chip Path

This path keeps the RP2040 native USB-C port connected to the PC as a serial command channel, and exposes the original Xbox controller device through Pico-PIO-USB on GPIO pins.

## Wiring

Default firmware pins:

| Xbox cable | RP2040-Zero |
| ---------- | ----------- |
| USB D+ | GP2 through optional 22 ohm series resistor |
| USB D- | GP3 through optional 22 ohm series resistor |
| Black / GND | GND |
| Red / 5V | Leave disconnected for PC-powered development |
| Yellow | Not connected |

Add a 1.5k pull-up resistor from `GP2 / D+` to `3.3V`. Pico-PIO-USB device mode requires this external full-speed pull-up.

## Power

During development, power the RP2040 from the PC USB-C cable and connect the Xbox cable ground to RP2040 ground. Do not directly tie PC USB 5V and Xbox 5V together.

For a self-powered console-only version, the board can be powered from Xbox 5V, but then the PC USB connection should be data-only or power-isolated. The cleaned-up hardware revision should include explicit power selection or isolation.

## Build

```powershell
.\scripts\build_gpio_pio.ps1
```

The output UF2 is:

```text
build/gpio_pio/rp2040_ogxbox_controller_gpio_pio.uf2
```

The D+ pin can be changed in `firmware/RP2040_OGXbox_Controller_GPIO_PIO/CMakeLists.txt` with `OGXB_PIO_USB_DP_PIN`. D- is the adjacent GPIO.

## Control

The PC sees the native USB-C port as a serial device. The same control helper works:

```powershell
.\tools\xidctl.ps1 -Auto -Status
.\tools\xidctl.ps1 -Auto -Tap UP -TapMs 120
.\tools\xidctl.ps1 -Auto -ScriptPath .\scripts\example_controls.xid
```

The experimental firmware boots neutral by default. The old idle Down/Up loop is disabled with `OGXB_ENABLE_IDLE_SCRIPT=0`.
