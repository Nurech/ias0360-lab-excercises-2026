#!/usr/bin/env bash
# flash_apps.sh   (run in the vm)
# both context switch apps: app1 .uf2 (has boot2) at the flash base, app2 .bin at XIP_BASE + APP2_OFFSET.
# same as lab_3.1/flash_program.sh, but for our one build dir and picotool from the image.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PICO="$REPO/notebook/picotool.sh"
B="$REPO/lab_3.1/build"
APP2_ADDR=0x10140000   # lab_3.1/lib/context/context.h APP2_OFFSET + 0x10000000

for f in "$B/application1.uf2" "$B/application2.bin"; do
    if [ ! -f "$f" ]; then
        echo "missing $f - build first:" >&2
        echo "    ./notebook/container.sh \"cd lab_3.1 && mkdir -p build && cd build && cmake .. && make -j6 application1 application2\"" >&2
        exit 1
    fi
done

echo ">> app1 -> 0x10000000"
"$PICO" load "$B/application1.uf2" -f
echo ">> app2 -> $APP2_ADDR"
"$PICO" load "$B/application2.bin" --offset "$APP2_ADDR"
"$PICO" reboot
echo ">> done. app1 starts; SWITCH APP on the lcd jumps to app2 and back."
