@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "BIN_FILE=%SCRIPT_DIR%splatoon-atoms3-merged.bin"

if not exist "%BIN_FILE%" (
  echo Error: Binary not found: %BIN_FILE%
  exit /b 1
)

set "PORT=%~1"
if "%PORT%"=="" (
  for /f "tokens=1" %%p in ('powershell -NoProfile -Command "Get-CimInstance Win32_SerialPort ^| Where-Object { $_.PNPDeviceID -like 'USB*' } ^| Select-Object -First 1 -ExpandProperty DeviceID"') do set "PORT=%%p"
)

if "%PORT%"=="" (
  echo Error: Could not auto-detect AtomS3 serial port.
  echo Usage: flash_merged.bat COM3
  exit /b 1
)

set "ESPTOOL="
where esptool.py >nul 2>&1 && set "ESPTOOL=esptool.py"
if "%ESPTOOL%"=="" (
  where esptool >nul 2>&1 && set "ESPTOOL=esptool"
)

if "%ESPTOOL%"=="" (
  echo Error: esptool not found in PATH.
  echo Install Python esptool or PlatformIO first.
  exit /b 1
)

echo Flashing %BIN_FILE%
echo Port: %PORT%
%ESPTOOL% --chip esp32s3 --port %PORT% --baud 460800 write_flash 0x0 "%BIN_FILE%"
if errorlevel 1 exit /b 1
echo Done.
exit /b 0
