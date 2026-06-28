#!/usr/bin/env bash
set -euo pipefail

# Capture emulator screenshots for every minute from 00:00 to 00:59.
# Usage:
#   scripts/capture-0000-0059.sh [platform] [output_dir]
# Example:
#   scripts/capture-0000-0059.sh emery screenshots-0000-0059

PLATFORM="${1:-emery}"
OUTPUT_DIR="${2:-tmp/screenshots-0000-0059-${PLATFORM}}"

mkdir -p "${OUTPUT_DIR}"

for minute in $(seq 0 59); do
  hhmmss=$(printf "00:%02d:00" "${minute}")
  filename=$(printf "%s/00-%02d.png" "${OUTPUT_DIR}" "${minute}")

  pebble emu-set-time "${hhmmss}" --emulator "${PLATFORM}"
  sleep 0.2
  pebble screenshot "${filename}" --emulator "${PLATFORM}" --no-open

  echo "Captured ${filename}"
done

echo "Done. Screenshots saved to ${OUTPUT_DIR}"
