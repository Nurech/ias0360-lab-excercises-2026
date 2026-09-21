#!/usr/bin/env bash
# serial. stay at 115200, 1200 baud reboots the board.
set -euo pipefail

if [ -n "${PICO_PORT:-}" ]; then
    PORT="$PICO_PORT"
elif [ -e /dev/pico ]; then
    PORT=/dev/pico
elif [ -e /dev/ttyACM0 ]; then
    PORT=/dev/ttyACM0
else
    PORT=/dev/pico
fi

board_state() {
    local d vid pid
    for d in /sys/bus/usb/devices/*/; do
        [ -f "$d/idVendor" ] || continue
        vid="$(cat "$d/idVendor")"
        [ "$vid" = "2e8a" ] || continue
        pid="$(cat "$d/idProduct")"
        case "$pid" in
            0003) echo "bootsel" ; return ;;
            *)    echo "running" ; return ;;
        esac
    done
    echo "absent"
}

if [ ! -e "$PORT" ]; then
    case "$(board_state)" in
        absent)
            echo "No Raspberry Pi device on USB at all." >&2
            echo "The eval board has TWO micro-usb ports - the other one only feeds the LCD." >&2
            ;;
        bootsel)
            echo "The board is in BOOTSEL: it is a usb drive right now, not a serial port." >&2
            echo "Flash something to bring it back:  ./notebook/flash.sh <project>" >&2
            ;;
        running)
            echo "The board is running, but presents no serial device." >&2
            echo "That means this firmware was built without USB stdio - add" >&2
            echo "pico_enable_stdio_usb(<target> 1) to its CMakeLists.txt and reflash." >&2
            ;;
    esac
    exit 1
fi

stty -F "$PORT" raw 115200 -echo -hupcl

if [ $# -ge 1 ]; then
    echo ">> reading $PORT for $1 s"
    timeout "$1" cat "$PORT" || true
    exit 0
fi

echo ">> reading $PORT - ctrl-c to stop"
exec cat "$PORT"
