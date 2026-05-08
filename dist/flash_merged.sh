#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_FILE="$SCRIPT_DIR/splatoon-atoms3-merged.bin"

if [[ ! -f "$BIN_FILE" ]]; then
  echo "Error: Binary not found: $BIN_FILE" >&2
  exit 1
fi

PORT="${1:-}"
if [[ -z "$PORT" ]]; then
  for p in /dev/cu.usbmodem* /dev/tty.usbmodem*; do
    if [[ -e "$p" ]]; then
      PORT="$p"
      break
    fi
  done
fi

if [[ -z "$PORT" ]]; then
  echo "Error: Could not auto-detect AtomS3 serial port." >&2
  echo "Usage: ./flash_merged.sh /dev/cu.usbmodemXXXX" >&2
  exit 1
fi

if command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL_CMD=(esptool.py)
elif command -v esptool >/dev/null 2>&1; then
  ESPTOOL_CMD=(esptool)
elif [[ -x "$HOME/.platformio/penv/bin/python" && -f "$HOME/.platformio/packages/tool-esptoolpy/esptool.py" ]]; then
  ESPTOOL_CMD=("$HOME/.platformio/penv/bin/python" "$HOME/.platformio/packages/tool-esptoolpy/esptool.py")
else
  echo "Error: esptool not found." >&2
  echo "Install one of: esptool.py, esptool, or PlatformIO Core." >&2
  exit 1
fi

echo "Flashing $BIN_FILE"
echo "Port: $PORT"
"${ESPTOOL_CMD[@]}" --chip esp32s3 --port "$PORT" --baud 460800 write_flash 0x0 "$BIN_FILE"
echo "Done."
