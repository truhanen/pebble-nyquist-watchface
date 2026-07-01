#!/usr/bin/env bash
set -euo pipefail

# Capture exactly four screenshots at 10:10 for Emery and Gabbro:
# - normal (white background)
# - inverted (black background)
#
# Usage:
#   scripts/create_1010_bw_screenshots.sh [--output-dir <path>]
# Example:
#   scripts/create_1010_bw_screenshots.sh --output-dir screenshots

OUTPUT_DIR=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --output-dir)
      if [[ $# -lt 2 ]]; then
        echo "Error: --output-dir requires a value." >&2
        exit 1
      fi
      OUTPUT_DIR="$2"
      shift 2
      ;;
    *)
      echo "Error: unknown argument '$1'." >&2
      echo "Usage: $0 [--output-dir <path>]" >&2
      exit 1
      ;;
  esac
done

if [[ -z "${OUTPUT_DIR}" ]]; then
  OUTPUT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/screenshots"
fi

mkdir -p "${OUTPUT_DIR}"

prepare_platform() {
  local platform="$1"
  # Start emulator first, wait a bit, then install and let the app boot.
  pebble emu-set-time "10:10:00" --emulator "${platform}"
  sleep 1
  pebble install --emulator "${platform}"
  sleep 1
}

capture_single() {
  local platform="$1"
  local mode="$2"
  local filename="${OUTPUT_DIR}/${platform}-${mode}.png"

  pebble emu-set-time "10:10:00" --emulator "${platform}"
  sleep 0.5
  pebble screenshot "${filename}" --emulator "${platform}" --no-open
  echo "Captured ${filename}"
}

send_normal_settings_save() {
  local platform="$1"
  pebble send-app-message \
    --emulator "${platform}" \
    --uint \
      10006=1 \
      10007=1 \
      10008=1 \
      10009=1 \
      10010=0 \
      10011=1 \
      10012=0
}

send_invert_settings_save() {
  local platform="$1"
  pebble send-app-message \
    --emulator "${platform}" \
    --uint \
      10006=1 \
      10007=1 \
      10008=1 \
      10009=1 \
      10010=1 \
      10011=1 \
      10012=0
}

prepare_platform "emery"
send_normal_settings_save "emery"
sleep 0.5
capture_single "emery" "normal"
send_invert_settings_save "emery"
sleep 0.5
capture_single "emery" "inverted"

pebble kill && pebble wipe

prepare_platform "gabbro"
send_normal_settings_save "gabbro"
sleep 0.5
capture_single "gabbro" "normal"
send_invert_settings_save "gabbro"
sleep 0.5
capture_single "gabbro" "inverted"

echo "Done. Screenshots saved to ${OUTPUT_DIR}"
