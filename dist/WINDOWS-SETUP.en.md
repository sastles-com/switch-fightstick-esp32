# AtomS3 Flashing Guide (Windows)

This is a beginner-friendly guide for flashing firmware to M5Stack AtomS3 using the distributed file `splatoon-dist.zip`.

---

## 0. Prerequisites

- Windows PC
- M5Stack AtomS3
- USB-C cable with data support
- Distribution file: `splatoon-dist.zip`

Notes:
- Flashing does not work with charge-only cables.
- Do not connect to Nintendo Switch yet. First connect directly to your Windows PC.

---

## 1. Install Required Tools

### 1-1. Install Python

1. Open: https://www.python.org/downloads/windows/
2. Download and run the latest Python 3.x installer.
3. Check **Add Python to PATH**.
4. Click Install Now.

Verify in Command Prompt:

```bat
python --version
```

### 1-2. Install esptool

Run in Command Prompt:

```bat
python -m pip install --user esptool
```

Verify:

```bat
esptool.py version
```

If `esptool.py` is not found:

```bat
python -m site --user-base
```

Add `Scripts` under the shown path to the `Path` environment variable.
Example: `C:\Users\<username>\AppData\Roaming\Python\Python3x\Scripts`

---

## 2. Extract The Distribution ZIP

Example for `Downloads` (PowerShell):

```powershell
cd $HOME\Downloads
Expand-Archive .\splatoon-dist.zip -DestinationPath .\splatoon-dist -Force
cd .\splatoon-dist
Get-ChildItem
```

You should see files like:

- `splatoon-atoms3-merged.bin`
- `flash_merged.bat`
- `flash_merged.ps1`
- `WINDOWS-SETUP.md`
- `README-distribution.txt`

---

## 3. Connect AtomS3 To Windows

Connect AtomS3 via USB-C.

Check COM ports (PowerShell):

```powershell
Get-CimInstance Win32_SerialPort | Select-Object DeviceID,Name
```

Examples:
- `COM3`
- `COM5`

---

## 4. Flash With Auto Script (Recommended)

### 4-1. Run from cmd

1. Open the `splatoon-dist` folder.
2. Type `cmd` in the address bar and press Enter.
3. Run:

```bat
flash_merged.bat
```

If auto-detection fails:

```bat
flash_merged.bat COM3
```

### 4-2. Run from PowerShell

```powershell
powershell -ExecutionPolicy Bypass -File .\flash_merged.ps1
```

To specify a port:

```powershell
powershell -ExecutionPolicy Bypass -File .\flash_merged.ps1 COM3
```

Flashing is complete when `Done.` appears.

---

## 5. Verify Operation

1. Unplug AtomS3 once.
2. Connect it to Nintendo Switch.
3. Confirm `Ready` appears on the device screen.

If you want to verify the built-in image (`plate.png`) but still see an old uploaded image:

1. Long-press BtnA for 2 seconds to open the Web UI.
2. Press `Use built-in image`.
3. Reboot and confirm it runs with the built-in image.

---

## 6. Troubleshooting

### 6-1. COM port does not appear

- Hold RESET to enter flashing mode.
- Change the cable (use a data-capable cable).
- Try another USB port.
- Unplug and reconnect.
- Open Device Manager and check `Ports (COM & LPT)`.

### 6-2. `esptool` not found

- Run `python -m pip install --user esptool` again.
- Add Python Scripts path to `Path`.
- Reopen a new terminal and retry.

### 6-3. Flashing fails

Run manual command and inspect logs:

```bat
esptool.py --chip esp32s3 --port COM3 --baud 460800 write_flash 0x0 splatoon-atoms3-merged.bin
```

---

## 7. Notes

- This distribution uses `splatoon-atoms3-merged.bin` (merged single image).
- Flash offset is fixed to `0x0`.
- This method is easy to overwrite in existing environments.
- Saved custom image (`/image.bin`) from the Web UI is not erased.
