#!/usr/bin/env bash
# picotool on the vm. host binary if you have it, else the course image.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE=ias0360-2026

if command -v picotool >/dev/null; then
    exec picotool "$@"
fi

if [ -f /.dockerenv ]; then
    echo "picotool is not on PATH in this container." >&2
    exit 1
fi

docker image inspect "$IMAGE" >/dev/null 2>&1 || {
    echo "image '$IMAGE' is missing. paste this first:" >&2
    echo "    docker build -t $IMAGE $REPO/own_pc_setup" >&2
    exit 1
}

exec docker run --rm --privileged --entrypoint picotool \
    -v /dev/bus/usb:/dev/bus/usb \
    -v "$REPO:$REPO" -w "$REPO" \
    "$IMAGE" "$@"
