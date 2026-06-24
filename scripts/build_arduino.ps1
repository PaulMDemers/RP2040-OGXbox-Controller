param(
    [string]$ArduinoCli = "C:\Program Files\Arduino CLI\arduino-cli.exe"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$packageUrl = "https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json"

& $ArduinoCli compile `
    --export-binaries `
    --fqbn "rp2040:rp2040:waveshare_rp2040_zero:usbstack=tinyusb" `
    --additional-urls $packageUrl `
    (Join-Path $repoRoot "firmware\RP2040_OGXbox_Controller")

& $ArduinoCli compile `
    --export-binaries `
    --fqbn "rp2040:rp2040:waveshare_rp2040_zero" `
    --additional-urls $packageUrl `
    (Join-Path $repoRoot "firmware\RP2040_UART_Bridge")
