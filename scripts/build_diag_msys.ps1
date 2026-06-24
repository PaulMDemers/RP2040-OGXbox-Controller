param(
    [string]$MsysBash = "C:\msys64\usr\bin\bash.exe",
    [string]$NxdkDir = $env:NXDK_DIR
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $MsysBash)) {
    throw "MSYS2 bash was not found at $MsysBash"
}

if (-not $NxdkDir) {
    $repoRoot = Split-Path -Parent $PSScriptRoot
    $localNxdk = Join-Path $repoRoot ".nxdk"
    $siblingNxdk = Join-Path (Split-Path -Parent $repoRoot) ".nxdk"

    if (Test-Path $localNxdk) {
        $NxdkDir = $localNxdk
    } elseif (Test-Path $siblingNxdk) {
        $NxdkDir = $siblingNxdk
    }
} else {
    $repoRoot = Split-Path -Parent $PSScriptRoot
}

if (-not $NxdkDir -or -not (Test-Path $NxdkDir)) {
    throw "nxdk was not found. Pass -NxdkDir or set NXDK_DIR."
}

$diagDir = Join-Path $repoRoot "diagnostics\xid_usb_diag"

function Convert-ToMsysPath([string]$Path) {
    $resolved = (Resolve-Path $Path).Path
    $drive = $resolved.Substring(0, 1).ToLowerInvariant()
    $rest = $resolved.Substring(2).Replace("\", "/")
    return "/$drive$rest"
}

$diagMsys = Convert-ToMsysPath $diagDir
$nxdkMsys = Convert-ToMsysPath $NxdkDir

& $MsysBash -lc "export MSYSTEM=MINGW64; export PATH=/mingw64/bin:$nxdkMsys/bin:`$PATH; cd '$diagMsys' && make NXDK_DIR='$nxdkMsys'"
