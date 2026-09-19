# 抓取 COM3 的原始字节到文件（不经过 PowerShell 的文本编码，避免字节被替换成 '?'）
# 用法: powershell -NoProfile -File capture_com3.ps1 <输出文件> <秒数>
param(
    [string]$OutFile = "com3.bin",
    [int]$Seconds = 10
)

$port = New-Object System.IO.Ports.SerialPort COM3,115200,None,8,one
$port.ReadTimeout = 500
$port.Open()
# 注意：不要动 DtrEnable/RtsEnable。部分 DAPLink 固件把 CDC 的 DTR 映射到目标 nRESET，
# 置位会让芯片不停复位，串口里看到的就是反复出现的启动横幅。

$ms = New-Object System.IO.MemoryStream
$buf = New-Object byte[] 4096
$deadline = (Get-Date).AddSeconds($Seconds)

while ((Get-Date) -lt $deadline) {
    try {
        $n = $port.Read($buf, 0, $buf.Length)
        if ($n -gt 0) { $ms.Write($buf, 0, $n) }
    } catch [TimeoutException] {
        # 正常：这段时间没有新数据
    }
}

$port.Close()
[System.IO.File]::WriteAllBytes($OutFile, $ms.ToArray())
Write-Output ("captured {0} bytes" -f $ms.Length)
