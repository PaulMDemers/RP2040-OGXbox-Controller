param(
    [int]$Port = 49036,
    [string]$LogPath = (Join-Path $PSScriptRoot "udp_diag.log")
)

$ErrorActionPreference = "Stop"
$dir = Split-Path -Parent $LogPath
if ($dir -and -not (Test-Path $dir)) {
    New-Item -ItemType Directory -Path $dir | Out-Null
}

"# UDP diag listener started $(Get-Date -Format o) port=$Port" | Out-File -FilePath $LogPath -Encoding utf8 -Append

$endpoint = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, $Port)
$client = New-Object System.Net.Sockets.UdpClient
$client.Client.SetSocketOption([System.Net.Sockets.SocketOptionLevel]::Socket, [System.Net.Sockets.SocketOptionName]::ReuseAddress, $true)
$client.Client.Bind($endpoint)

try {
    while ($true) {
        $remote = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
        $bytes = $client.Receive([ref]$remote)
        $text = [System.Text.Encoding]::UTF8.GetString($bytes).TrimEnd()
        $line = "{0} {1}:{2} {3}" -f (Get-Date -Format o), $remote.Address, $remote.Port, $text
        $line | Out-File -FilePath $LogPath -Encoding utf8 -Append
        Write-Output $line
    }
}
finally {
    $client.Close()
}
