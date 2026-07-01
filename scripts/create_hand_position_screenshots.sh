#!/usr/bin/env bash
set -euo pipefail

# Capture emulator screenshots for minutes 12:00-12:15 for both Emery and Gabbro
# using only the normal (white background) settings.
#
# Usage:
#   scripts/create_minute_screenshots.sh [--output-dir <path>]
# Example:
#   scripts/create_minute_screenshots.sh --output-dir tmp/screenshots-1200-1215-white

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
  OUTPUT_DIR="tmp/screenshots-1200-1215-white"
fi

prepare_platform() {
  local platform="$1"
  # Start emulator first, wait a bit, then install and let the app boot.
  pebble emu-set-time "12:00:00" --emulator "${platform}"
  sleep 1
  pebble install --emulator "${platform}"
  sleep 1
}

capture_platform_minutes() {
  local platform="$1"
  local outdir="$2"
  mkdir -p "${outdir}"

  for minute in $(seq 0 15); do
    hhmmss=$(printf "12:%02d:00" "${minute}")
    filename=$(printf "%s/12-%02d.png" "${outdir}" "${minute}")

    pebble emu-set-time "${hhmmss}" --emulator "${platform}"
    sleep 0.5
    pebble screenshot "${filename}" --emulator "${platform}" --no-open

    echo "Captured ${filename}"
  done
}

send_normal_settings_save() {
  local platform="$1"
  # "Settings save" style message with InvertColors=0.
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

prepare_platform "emery"
send_normal_settings_save "emery"
sleep 0.5
capture_platform_minutes "emery" "${OUTPUT_DIR}/emery"

# Switch emulator family before starting Gabbro.
pebble kill && pebble wipe

prepare_platform "gabbro"
send_normal_settings_save "gabbro"
sleep 0.5
capture_platform_minutes "gabbro" "${OUTPUT_DIR}/gabbro"

echo "Done. White-background screenshots saved under ${OUTPUT_DIR}/{emery,gabbro}"
