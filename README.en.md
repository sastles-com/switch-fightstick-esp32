# Switch Fightstick - Splatoon Plate Printer

This tool makes an M5Stack AtomS3 appear as a Nintendo Switch USB HID controller and automatically draws a 320x120 1-bit image on a Splatoon name plate.

## Overview

- Board: M5Stack AtomS3 (ESP32-S3)
- Connection: USB-C -> Nintendo Switch
- HID identity: HORI POKKEN CONTROLLER compatible
- Image sources:
  - Built-in `plate.png` bundled in firmware
  - Custom image uploaded from the Web UI

## Quick Start

1. Open the name plate editor screen on Nintendo Switch.
2. Clear the canvas so it is blank.
3. Select the smallest brush.
4. Move the cursor to the top-left corner.
5. Connect AtomS3 to the Switch.
6. When `Ready` appears on screen, short-press BtnA.

## Operation

### Normal Drawing

1. Connect AtomS3 to the Switch.
2. `Ready` appears on the device screen.
3. Short-press BtnA to start drawing.
4. During drawing, coordinates, progress percentage, and a progress bar are shown.
5. After `Done` appears, short-press BtnA to run again.

### BtnA Action Table

| State | BtnA Short Press | BtnA Long Press |
|---|---|---|
| Ready / Done | Start drawing / Run again | Enter QR mode |
| Drawing | Stop immediately | N/A |
| QR mode | Exit and return to normal screen | N/A |

Long-press threshold is 2 seconds.

## Web UI

### How To Open

1. In normal screen mode, long-press BtnA for 2 seconds.
2. AtomS3 starts AP mode.
3. Scan the Wi-Fi QR code on the screen and connect to `AtomS3-ImageUI`.
4. Open `http://192.168.4.1` in your browser.

Notes:

- Depending on OS behavior, a captive portal may open automatically.
- Captive portal window sizing is controlled by the OS.
- `Open in browser` may fail to launch an external browser due to OS restrictions.

### What You Can Do In The Web UI

- Upload PNG / JPEG / WEBP / BMP
- Preview conversion to 320x120 / 1-bit
- Adjust Threshold / Invert / Dither
- Adjust Preview scale
- Adjust Page height
- Save Tuning values
- Return to built-in image with `Use built-in image`

### Priority Between Built-In Image And Custom Image

At boot, images are loaded in this order:

1. If `/image.bin` exists on LittleFS, use it.
2. Otherwise use the built-in image from firmware.

Because of this, even if you update `plate.png` and flash new firmware, the visible image will not change if a previously uploaded custom image remains on the device.
If you want to use the built-in image, press `Use built-in image` in the Web UI to delete `/image.bin`.

## For Developers: Build And Flash

### Build

```bash
/Users/katano/Library/Python/3.9/bin/pio run -e atoms3_arduino
```

### Flash

```bash
/Users/katano/Library/Python/3.9/bin/pio run -e atoms3_arduino -t upload --upload-port /dev/cu.usbmodem1101
```

### Regenerate Built-In Image Data

After updating `plate.png`, regenerate `src/image_data.c` with:

```bash
python3 png2c.py -t esp32 -o src/image_data.c plate.png
```

Then rebuild the firmware.

### Inspect Image Data

```bash
python3 debug_image.py
```

This outputs files such as `debug_reconstructed.png`, useful for checking the contents of `src/image_data.c`.

## Distribution Files

Distribution artifacts are stored in `dist/`.

- Distribution overview: `dist/README-distribution.txt`
- macOS setup: `dist/MAC-SETUP.md`
- Windows setup: `dist/WINDOWS-SETUP.md`

Main artifacts:

- `splatoon-atoms3-merged.bin`: merged single-image firmware
- `flash_merged.sh`: auto-flash script for macOS / Linux
- `flash_merged.bat`: auto-flash script for Windows cmd
- `flash_merged.ps1`: auto-flash script for Windows PowerShell

## File Layout

```text
src/
  main.cpp         Main logic (HID / Web UI / LCD rendering)
  image_data.c     Built-in image data
  image_data.h
dist/
  README-distribution.txt
  MAC-SETUP.md
  WINDOWS-SETUP.md
  flash_merged.sh
  flash_merged.bat
  flash_merged.ps1
png2c.py           Conversion script: plate.png -> src/image_data.c
plate.png          Built-in default image
debug_image.py     Reconstruction checker for image_data.c
```

## API Endpoints

| Endpoint | Method | Description |
|---|---|---|
| `/` | GET | Web UI |
| `/api/status` | GET | Status JSON |
| `/api/image` | GET | Currently active 1-bit image data |
| `/api/upload` | POST | Upload custom image |
| `/api/clear` | POST | Delete custom image and return to built-in image |
| `/api/tuning` | GET | Get tuning values |
| `/api/tuning` | POST | Save tuning values |
| `/api/done` | POST | Reboot |

## References

- https://github.com/shinyquagsire23/Switch-Fightstick
- https://github.com/Loloweb/Switch-Fightstick
- https://github.com/progmem/Switch-Fightstick
