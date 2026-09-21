#!/usr/bin/env bash
# reset.sh [run|bootsel|erase]
set -euo pipefail

MODE="${1:-run}"

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PICO="$REPO/notebook/picotool.sh"

case "$MODE" in
    run)
        echo ">> rebooting into the application"
        "$PICO" reboot -a -f
        ;;
    bootsel)
        echo ">> rebooting into BOOTSEL"
        "$PICO" reboot -u -f
        ;;
    erase)
        echo ">> erasing all of flash"
        "$PICO" erase -a -f
        echo ">> erased. the board is in BOOTSEL and will stay quiet until you flash it."
        ;;
    *)
        echo "usage: $(basename "$0") [run|bootsel|erase]" >&2
        exit 1
        ;;
esac
