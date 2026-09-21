#!/usr/bin/env bash
# flash.sh lab_1_1/daq   (run in the vm)
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ $# -ne 1 ]; then
    echo "usage: $(basename "$0") <project-dir|file.uf2>" >&2
    echo "   eg: $(basename "$0") lab_1_1/daq" >&2
    exit 1
fi

if [ -f "$1" ]; then
    UF2="$1"
else
    DIR="$1"
    [ -d "$DIR" ] || DIR="$REPO/$1"
    UF2="$(find "$DIR/build" -maxdepth 1 -name '*.uf2' -print -quit 2>/dev/null || true)"
    if [ -z "$UF2" ]; then
        echo "no .uf2 under $DIR/build - build it first:" >&2
        echo "    ./notebook/container.sh \"cd $1 && mkdir -p build && cd build && cmake .. && make -j6\"" >&2
        exit 1
    fi
fi

UF2="$(cd "$(dirname "$UF2")" && pwd)/$(basename "$UF2")"
PICO="$REPO/notebook/picotool.sh"

echo ">> flashing $UF2"
for attempt in 1 2 3; do
    if "$PICO" load "$UF2" -f -x; then
        echo ">> done. watch it with ./notebook/logs.sh"
        exit 0
    fi
    if [ "$attempt" -lt 3 ]; then
        echo ">> attempt $attempt failed, retrying in 2s"
        sleep 2
    fi
done

echo "flash failed three times." >&2
echo "check: the board is on the DATA micro-usb port, not the LCD one, and that" >&2
echo "lsusb shows 2e8a:000a (running) or 2e8a:0003 (bootsel)." >&2
exit 1
