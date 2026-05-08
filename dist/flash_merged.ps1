$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$binFile = Join-Path $scriptDir 'splatoon-atoms3-merged.bin'

if (!(Test-Path $binFile)) {
    Write-Error "Binary not found: $binFile"
}

$port = if ($args.Count -ge 1) { $args[0] } else {
    (Get-CimInstance Win32_SerialPort |
        Where-Object { $_.PNPDeviceID -like 'USB*' } |
        Select-Object -First 1 -ExpandProperty DeviceID)
}

if ([string]::IsNullOrWhiteSpace($port)) {
    throw 'Could not auto-detect AtomS3 serial port. Usage: .\flash_merged.ps1 COM3'
}

$esptoolCmd = Get-Command esptool.py -ErrorAction SilentlyContinue
if (-not $esptoolCmd) {
    $esptoolCmd = Get-Command esptool -ErrorAction SilentlyContinue
}
if (-not $esptoolCmd) {
    throw 'esptool not found in PATH. Install Python esptool or PlatformIO first.'
}

Write-Host "Flashing $binFile"
Write-Host "Port: $port"
& $esptoolCmd.Source --chip esp32s3 --port $port --baud 460800 write_flash 0x0 $binFile
Write-Host 'Done.'
