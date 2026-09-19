# Capture raw bytes from COM3 into a file.
#
# Why raw bytes: PowerShell's text pipeline replaces non-ASCII bytes with '?',
# which corrupts captured serial output. Writing bytes avoids that entirely.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File capture_com3.ps1 <outfile> <seconds>
#
# NOTE: keep this file pure ASCII. Windows PowerShell 5.1 reads .ps1 files using
# the system ANSI code page (GBK on a Chinese Windows) unless the file has a UTF-8
# BOM. Non-ASCII bytes in comments get mis-decoded and can produce stray quotes
# that break parsing with a confusing "UnexpectedToken" error.
#
# NOTE: do NOT touch DtrEnable/RtsEnable. Some DAPLink firmwares wire the CDC DTR
# line to the target nRESET, so asserting it resets the MCU in a loop -- which
# shows up as a boot banner repeating in the captured log.

param(
    [string]$OutFile = "com3.bin",
    [int]$Seconds = 10
)

$port = New-Object System.IO.Ports.SerialPort COM3,115200,None,8,one
$port.ReadTimeout = 500

# If the port is held by another program (a serial terminal, or a previous
# capture process that did not exit), fail fast. Otherwise every Read() below
# throws and floods the console.
try {
    $port.Open()
} catch {
    $msg = $_.Exception.Message
    Write-Output ("ERROR: cannot open COM3 -> " + $msg)
    Write-Output "Close whatever holds the port (serial terminal / stale capture process) and retry."
    exit 1
}

$ms = New-Object System.IO.MemoryStream
$buf = New-Object byte[] 4096
$deadline = (Get-Date).AddSeconds($Seconds)

while ((Get-Date) -lt $deadline) {
    try {
        $n = $port.Read($buf, 0, $buf.Length)
        if ($n -gt 0) { $ms.Write($buf, 0, $n) }
    } catch [TimeoutException] {
        # Expected: no new data during this window.
    }
}

$port.Close()
[System.IO.File]::WriteAllBytes($OutFile, $ms.ToArray())
Write-Output ("captured {0} bytes" -f $ms.Length)
