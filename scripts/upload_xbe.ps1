param(
    [string]$HostName = "192.168.50.156",
    [string]$User = "xbox",
    [string]$Password = "xbox",
    [string]$RemoteDir = "/E/Applications/xib_diag",
    [string]$LocalXbe = "",
    [string]$RemoteName = "default.xbe"
)

$ErrorActionPreference = "Stop"

if (-not $LocalXbe) {
    $repoRoot = Split-Path -Parent $PSScriptRoot
    $LocalXbe = Join-Path $repoRoot "diagnostics\xid_usb_diag\bin\default.xbe"
}

if (-not (Test-Path $LocalXbe)) {
    throw "Missing local XBE: $LocalXbe"
}

function Read-FtpResponse([System.IO.StreamReader]$Reader) {
    $lines = [System.Collections.Generic.List[string]]::new()
    while ($true) {
        $line = $Reader.ReadLine()
        if ($null -eq $line) {
            throw "FTP connection closed while reading response"
        }

        $lines.Add($line)
        if ($line.Length -ge 4 -and $line.Substring(0, 3) -match "^\d{3}$" -and $line[3] -eq " ") {
            break
        }
    }

    return ($lines -join "`n")
}

function Send-FtpCommand(
    [System.IO.StreamWriter]$Writer,
    [System.IO.StreamReader]$Reader,
    [string]$Command
) {
    $Writer.WriteLine($Command)
    $Writer.Flush()
    return Read-FtpResponse $Reader
}

function Open-PasvDataConnection(
    [System.IO.StreamWriter]$Writer,
    [System.IO.StreamReader]$Reader
) {
    $resp = Send-FtpCommand $Writer $Reader "PASV"
    if ($resp -notmatch "\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\)") {
        throw "Could not parse PASV response: $resp"
    }

    $dataHost = "$($Matches[1]).$($Matches[2]).$($Matches[3]).$($Matches[4])"
    $dataPort = ([int]$Matches[5] * 256) + [int]$Matches[6]
    $data = [System.Net.Sockets.TcpClient]::new()
    $data.Connect($dataHost, $dataPort)
    return $data
}

function Invoke-FtpUpload {
    $bytes = [System.IO.File]::ReadAllBytes($LocalXbe)
    Write-Output "Uploading $LocalXbe ($($bytes.Length) bytes) to ftp://$HostName$RemoteDir/$RemoteName"

    $ctrl = [System.Net.Sockets.TcpClient]::new()
    $ctrl.Connect($HostName, 21)
    $stream = $ctrl.GetStream()
    $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::ASCII)
    $writer = [System.IO.StreamWriter]::new($stream, [System.Text.Encoding]::ASCII)
    $writer.NewLine = "`r`n"
    $writer.AutoFlush = $true

    try {
        Read-FtpResponse $reader | Write-Output
        Send-FtpCommand $writer $reader "USER $User" | Write-Output
        Send-FtpCommand $writer $reader "PASS $Password" | Write-Output
        Send-FtpCommand $writer $reader "TYPE I" | Write-Output
        Send-FtpCommand $writer $reader "CWD $RemoteDir" | Write-Output

        $data = Open-PasvDataConnection $writer $reader
        $writer.WriteLine("STOR $RemoteName")
        $writer.Flush()
        Read-FtpResponse $reader | Write-Output

        $dataStream = $data.GetStream()
        $dataStream.Write($bytes, 0, $bytes.Length)
        $dataStream.Close()
        $data.Close()
        Read-FtpResponse $reader | Write-Output

        $data = Open-PasvDataConnection $writer $reader
        $writer.WriteLine("LIST")
        $writer.Flush()
        Read-FtpResponse $reader | Write-Output
        $listReader = [System.IO.StreamReader]::new($data.GetStream())
        $listing = $listReader.ReadToEnd()
        $listReader.Close()
        $data.Close()
        Read-FtpResponse $reader | Write-Output
        $listing | Write-Output

        Send-FtpCommand $writer $reader "QUIT" | Out-Null
    } finally {
        $writer.Close()
        $reader.Close()
        $stream.Close()
        $ctrl.Close()
    }
}

Invoke-FtpUpload
