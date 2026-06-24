param(
    [string]$Port,
    [int]$Baud = 115200,
    [string[]]$Command,
    [switch]$Demo,
    [switch]$Interactive,
    [switch]$ListPorts
)

$ErrorActionPreference = "Stop"

function Show-Ports {
    [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object | ForEach-Object {
        Write-Output $_
    }
}

if ($ListPorts) {
    Show-Ports
    exit 0
}

if (-not $Port) {
    Write-Host "Available serial ports:"
    Show-Ports
    throw "Pass -Port COMx"
}

$serial = [System.IO.Ports.SerialPort]::new($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.NewLine = "`n"
$serial.ReadTimeout = 150
$serial.WriteTimeout = 1000
$serial.DtrEnable = $true
$serial.RtsEnable = $true

function Read-Responses([System.IO.Ports.SerialPort]$Serial, [int]$Ms = 150) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($Ms)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $line = $Serial.ReadLine()
            if ($line) {
                Write-Host ("< " + $line.Trim())
            }
        } catch [TimeoutException] {
            Start-Sleep -Milliseconds 10
        }
    }
}

function Send-Line([System.IO.Ports.SerialPort]$Serial, [string]$Line, [int]$DelayMs = 80) {
    Write-Host ("> " + $Line)
    $Serial.Write($Line + "`n")
    Read-Responses $Serial 180
    Start-Sleep -Milliseconds $DelayMs
}

try {
    $serial.Open()
    Start-Sleep -Milliseconds 500
    Read-Responses $serial 500

    if ($Demo) {
        foreach ($line in @("DOWN 150", "UP 150", "CLEAR")) {
            Send-Line $serial $line 180
        }
    }

    if ($Command) {
        foreach ($line in $Command) {
            Send-Line $serial $line 80
        }
    }

    if ($Interactive -or (-not $Demo -and -not $Command)) {
        Write-Host "Interactive mode. Type commands, or 'exit'."
        while ($true) {
            $line = Read-Host "xid"
            if ($line -eq "exit" -or $line -eq "quit") {
                break
            }
            if ($line.Trim().Length -eq 0) {
                Read-Responses $serial 200
                continue
            }
            Send-Line $serial $line 20
        }
    }
}
finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
}
