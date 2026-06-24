param(
    [string]$Port,
    [int]$Baud = 115200,
    [string[]]$Command,
    [string]$ScriptPath,
    [string]$Tap,
    [int]$TapMs = 120,
    [string]$Hold,
    [string]$Release,
    [string]$Axis,
    [int]$AxisValue = 0,
    [string]$Trigger,
    [int]$TriggerValue = 0,
    [switch]$Clear,
    [switch]$Status,
    [switch]$Demo,
    [switch]$Interactive,
    [switch]$ListPorts,
    [switch]$Auto,
    [int]$ResponseMs = 180
)

$ErrorActionPreference = "Stop"

function Show-Ports {
    [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object | ForEach-Object {
        Write-Output $_
    }
}

function New-XidSerial([string]$Name) {
    $serial = [System.IO.Ports.SerialPort]::new($Name, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $serial.NewLine = "`n"
    $serial.ReadTimeout = 120
    $serial.WriteTimeout = 1000
    $serial.DtrEnable = $true
    $serial.RtsEnable = $true
    return $serial
}

function Read-Responses([System.IO.Ports.SerialPort]$Serial, [int]$Ms = $ResponseMs, [switch]$Quiet) {
    $lines = New-Object System.Collections.Generic.List[string]
    $deadline = [DateTime]::UtcNow.AddMilliseconds($Ms)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $line = $Serial.ReadLine()
            if ($line) {
                $trimmed = $line.Trim()
                $lines.Add($trimmed)
                if (-not $Quiet) {
                    Write-Host ("< " + $trimmed)
                }
            }
        } catch [TimeoutException] {
            Start-Sleep -Milliseconds 10
        }
    }
    return $lines
}

function Send-Line([System.IO.Ports.SerialPort]$Serial, [string]$Line, [int]$DelayMs = 80) {
    $trimmed = $Line.Trim()
    if ($trimmed.Length -eq 0 -or $trimmed.StartsWith("#")) {
        return
    }

    if ($trimmed -match "^(?i)WAIT\s+(\d+)$") {
        Start-Sleep -Milliseconds ([int]$Matches[1])
        return
    }

    Write-Host ("> " + $trimmed)
    $Serial.Write($trimmed + "`n")
    Read-Responses $Serial $ResponseMs | Out-Null
    Start-Sleep -Milliseconds $DelayMs
}

function Test-XidPort([string]$Name) {
    $serial = New-XidSerial $Name
    try {
        $serial.Open()
        Start-Sleep -Milliseconds 250
        Read-Responses $serial 250 -Quiet | Out-Null
        $serial.Write("PING`n")
        $responses = Read-Responses $serial 450 -Quiet
        return ($responses | Where-Object { $_ -eq "OK PONG" } | Select-Object -First 1) -ne $null
    } catch {
        return $false
    } finally {
        if ($serial.IsOpen) {
            $serial.Close()
        }
    }
}

function Resolve-XidPort {
    if ($Port) {
        return $Port
    }

    if (-not $Auto) {
        Write-Host "Available serial ports:"
        Show-Ports
        throw "Pass -Port COMx or -Auto"
    }

    foreach ($candidate in (Show-Ports)) {
        if (Test-XidPort $candidate) {
            return $candidate
        }
    }

    throw "No XID controller port answered PING"
}

function Get-ScriptCommands([string]$Path) {
    if (-not (Test-Path $Path)) {
        throw "Script file not found: $Path"
    }

    Get-Content $Path | ForEach-Object {
        $line = $_.Trim()
        if ($line.Length -gt 0 -and -not $line.StartsWith("#")) {
            $line
        }
    }
}

if ($ListPorts) {
    Show-Ports
    exit 0
}

$commands = New-Object System.Collections.Generic.List[string]

if ($Demo) {
    "DOWN 150", "WAIT 180", "UP 150", "WAIT 180", "CLEAR" | ForEach-Object { $commands.Add($_) }
}

if ($Tap) {
    $commands.Add(("TAP {0} {1}" -f $Tap, $TapMs))
}

if ($Hold) {
    $commands.Add(("HOLD {0}" -f $Hold))
}

if ($Release) {
    $commands.Add(("RELEASE {0}" -f $Release))
}

if ($Axis) {
    $commands.Add(("AXIS {0} {1}" -f $Axis, $AxisValue))
}

if ($Trigger) {
    $commands.Add(("TRIG {0} {1}" -f $Trigger, $TriggerValue))
}

if ($Clear) {
    $commands.Add("CLEAR")
}

if ($Status) {
    $commands.Add("STATUS")
}

if ($Command) {
    foreach ($line in $Command) {
        $commands.Add($line)
    }
}

if ($ScriptPath) {
    foreach ($line in (Get-ScriptCommands $ScriptPath)) {
        $commands.Add($line)
    }
}

$resolvedPort = Resolve-XidPort
$serial = New-XidSerial $resolvedPort

try {
    Write-Host "Opening $resolvedPort at $Baud"
    $serial.Open()
    Start-Sleep -Milliseconds 500
    Read-Responses $serial 500 | Out-Null

    foreach ($line in $commands) {
        Send-Line $serial $line 80
    }

    if ($Interactive -or ($commands.Count -eq 0)) {
        Write-Host "Interactive mode. Type commands, WAIT ms, blank to read, or exit."
        while ($true) {
            $line = Read-Host "xid"
            if ($line -eq "exit" -or $line -eq "quit") {
                break
            }
            if ($line.Trim().Length -eq 0) {
                Read-Responses $serial 250 | Out-Null
                continue
            }
            Send-Line $serial $line 20
        }
    }
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
}
