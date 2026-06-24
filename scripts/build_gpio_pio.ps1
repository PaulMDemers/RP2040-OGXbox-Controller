param(
    [string]$BuildDir = "build\gpio_pio",
    [string]$PicoSdkPath,
    [string]$CMakePath,
    [string]$Generator = "Unix Makefiles"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$sourceDir = Join-Path $repoRoot "firmware\RP2040_OGXbox_Controller_GPIO_PIO"
$buildPath = Join-Path $repoRoot $BuildDir

if (-not $PicoSdkPath) {
    $arduinoSdk = Join-Path $env:LOCALAPPDATA "Arduino15\packages\rp2040\hardware\rp2040\5.6.1\pico-sdk"
    if (Test-Path $arduinoSdk) {
        $PicoSdkPath = $arduinoSdk
    }
}

if (-not $PicoSdkPath -or -not (Test-Path $PicoSdkPath)) {
    throw "Pico SDK not found. Pass -PicoSdkPath or install the Arduino-Pico RP2040 package."
}

$gccBin = Join-Path $env:LOCALAPPDATA "Arduino15\packages\rp2040\tools\pqt-gcc\4.1.0-1aec55e\bin"
if (Test-Path $gccBin) {
    $env:PATH = "$gccBin;$env:PATH"
}

$devkitMakeBin = "C:\devkitPro\msys2\usr\bin"
if (Test-Path $devkitMakeBin) {
    $env:PATH = "$devkitMakeBin;$env:PATH"
}

if (-not $CMakePath) {
    $vsCmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path $vsCmake) {
        $CMakePath = $vsCmake
    } else {
        $CMakePath = "cmake"
    }
}

$env:PICO_SDK_PATH = (Resolve-Path $PicoSdkPath).Path
New-Item -ItemType Directory -Force -Path $buildPath | Out-Null

& $CMakePath -S $sourceDir -B $buildPath -G $Generator
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE"
}

& $CMakePath --build $buildPath
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE"
}

$uf2Path = Join-Path $buildPath "rp2040_ogxbox_controller_gpio_pio.uf2"
if (-not (Test-Path $uf2Path)) {
    throw "Expected UF2 was not produced: $uf2Path"
}

Write-Host "Built: $uf2Path"
