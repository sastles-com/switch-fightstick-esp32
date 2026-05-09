Switch Fightstick (AtomS3) Distribution Binaries

Setup Guides
- macOS: `MAC-SETUP.en.md`
- Windows: `WINDOWS-SETUP.en.md`

Important
- `splatoon-atoms3-merged.bin` is a distribution firmware image that includes the built-in image.
- On devices that have uploaded an image from the Web UI, stored `/image.bin` is prioritized.
- To switch back to the built-in image, press `Use built-in image` in the Web UI.

File List
- splatoon-atoms3-merged.bin
  Merged single image. Flash it at offset 0x0.

- flash_merged.sh
  Auto-flash script for merged image.
  With no argument, it auto-detects `/dev/cu.usbmodem*` and flashes.

- flash_merged.bat
  Auto-flash script for Windows (cmd).
  With no argument, it auto-detects the USB serial port.

- flash_merged.ps1
  Auto-flash script for Windows (PowerShell).
  With no argument, it auto-detects the USB serial port.

- splatoon-atoms3-firmware.bin
  App image only. Flash at offset 0x10000.
  (Requires existing bootloader / partitions.)

- bootloader.bin
  Bootloader image (offset 0x0)

- partitions.bin
  Partition table image (offset 0x8000)

Recommended Flash Command (Merged Image)
- esptool.py --chip esp32s3 --port <PORT> --baud 460800 write_flash 0x0 splatoon-atoms3-merged.bin

Easiest Way (Auto Script)
- ./flash_merged.sh
- ./flash_merged.sh /dev/cu.usbmodem1101

Easy Way On Windows (Auto Script)
- flash_merged.bat
- flash_merged.bat COM3
- powershell -ExecutionPolicy Bypass -File .\flash_merged.ps1
- powershell -ExecutionPolicy Bypass -File .\flash_merged.ps1 COM3

Notes
- To run these scripts, `esptool.py` (or `esptool`) must be available in PATH.
- If a custom image is already saved on AtomS3, it is used instead of the built-in image.

Alternative Flash Command (Split Images)
- esptool.py --chip esp32s3 --port <PORT> --baud 460800 write_flash 0x0 bootloader.bin 0x8000 partitions.bin 0x10000 splatoon-atoms3-firmware.bin

PlatformIO command used in this project
- /Users/katano/Library/Python/3.9/bin/pio run -t upload --upload-port /dev/cu.usbmodem1101
