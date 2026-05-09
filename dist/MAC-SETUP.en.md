# AtomS3 Flashing Guide (macOS)

This is a beginner-friendly guide for flashing firmware to M5Stack AtomS3 using the distributed file `splatoon-dist.zip`.

---

## 0. Prerequisites

- Mac (macOS)
- M5Stack AtomS3
- USB-C cable with data support
- Distribution file: `splatoon-dist.zip`

Notes:
- Flashing does not work with charge-only cables.
- Do not connect to Nintendo Switch yet. First connect directly to your Mac.

---

## 1. Install Required Tools

### 1-1. Install Homebrew (only if not installed)

Open Terminal and run:

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

Verify:

```bash
brew --version
```

### 1-2. Install Python3

```bash
brew install python
```

Verify:

```bash
python3 --version
```

### 1-3. Install esptool

```bash
python3 -m pip install --user esptool
```

Verify:

```bash
esptool.py version
```

If you get `esptool.py: command not found`:

```bash
python3 -m site --user-base
```

Add `bin` under the displayed path to `PATH`.
(Example: append to `~/.zprofile`)

```bash
PY_USER_BIN="$(python3 -c 'import site; print(site.getuserbase() + "/bin")')"
echo "export PATH=\"$PY_USER_BIN:\$PATH\"" >> ~/.zprofile
source ~/.zprofile
```

Check again after restarting your shell.

---

## 2. Extract The Distribution ZIP

Example when the file is in `Downloads`:

```bash
cd ~/Downloads
unzip splatoon-dist.zip -d splatoon-dist
cd splatoon-dist
ls
```

You should see files like:

- `splatoon-atoms3-merged.bin`
- `flash_merged.sh`
- `MAC-SETUP.md`
- `README-distribution.txt`

---

## 3. Connect AtomS3 To Your Mac

Connect AtomS3 via USB-C.

Check available ports:

```bash
ls /dev/cu.usbmodem*
```

Example:
- `/dev/cu.usbmodem1101`

---

## 4. Flash With Auto Script (Recommended)

### 4-1. Add execute permission

```bash
chmod +x flash_merged.sh
```

### 4-2. Run flashing

```bash
./flash_merged.sh
```

If auto-detection fails:

```bash
./flash_merged.sh /dev/cu.usbmodem1101
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

### 6-1. Cannot connect / port does not appear

- Hold RESET to enter flashing mode.
- Change the cable (use a data-capable cable).
- Try another USB port.
- Unplug and reconnect.

### 6-2. `esptool` not found

- Run `python3 -m pip install --user esptool` again.
- Recheck your `PATH` settings.

### 6-3. Flashing fails

Run manual command and inspect logs:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodem1101 --baud 460800 write_flash 0x0 splatoon-atoms3-merged.bin
```

---

## 7. Notes

- This distribution uses `splatoon-atoms3-merged.bin` (merged single image).
- Flash offset is fixed to `0x0`.
- This method is easy to overwrite in existing environments.
- Saved custom image (`/image.bin`) from the Web UI is not erased.
