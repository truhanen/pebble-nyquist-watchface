#!/usr/bin/env bash
set -euo pipefail

# Capture emulator screenshots for every minute from 10:00 to 10:59.
# Usage:
#   scripts/capture-0000-0059.sh [--emulator <platform>] [--output-dir <path>]
# Example:
#   scripts/capture-0000-0059.sh --emulator emery --output-dir screenshots-1000-1059

PLATFORM="emery"
OUTPUT_DIR=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --emulator)
      if [[ $# -lt 2 ]]; then
        echo "Error: --emulator requires a value." >&2
        exit 1
      fi
      PLATFORM="$2"
      shift 2
      ;;
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
      echo "Usage: $0 [--emulator <platform>] [--output-dir <path>]" >&2
      exit 1
      ;;
  esac
done

if [[ -z "${OUTPUT_DIR}" ]]; then
  OUTPUT_DIR="tmp/screenshots-1000-1059-${PLATFORM}"
fi

mkdir -p "${OUTPUT_DIR}"

for minute in $(seq 0 59); do
  hhmmss=$(printf "10:%02d:00" "${minute}")
  filename=$(printf "%s/10-%02d.png" "${OUTPUT_DIR}" "${minute}")

  pebble emu-set-time "${hhmmss}" --emulator "${PLATFORM}"
  sleep 0.3
  pebble screenshot "${filename}" --emulator "${PLATFORM}" --no-open

  echo "Captured ${filename}"
done

echo "Done. Screenshots saved to ${OUTPUT_DIR}"
