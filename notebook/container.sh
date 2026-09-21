#!/usr/bin/env bash
# build box. no usb. flash from the vm.
set -euo pipefail

if [ -f /.dockerenv ]; then
    echo "Already in the container (repo at /root). Type exit, then run this again." >&2
    exit 1
fi

IMAGE=ias0360-2026
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

[ "$(uname -s)" = "Darwin" ] && export DOCKER_CONTEXT="${DOCKER_CONTEXT:-desktop-linux}"

docker info >/dev/null 2>&1 || {
    echo "docker is not responding. In the VM: sudo systemctl start docker" >&2
    exit 1
}

docker image inspect "$IMAGE" >/dev/null 2>&1 || {
    echo "image '$IMAGE' is missing. Build it once with:" >&2
    echo "    docker build -t $IMAGE $REPO/own_pc_setup" >&2
    exit 1
}

# uid 1000 so the share stays writable. skip the image entrypoint.
if [ $# -eq 0 ]; then
    echo ">> $IMAGE. This terminal IS the container now - type exit to get out."
    exec docker run --rm -it --user 1000:1000 --entrypoint bash \
        --hostname ias0360-container \
        -v "$REPO:/root" -v "$REPO:/media/sf_repo" -w /root "$IMAGE"
else
    exec docker run --rm --user 1000:1000 --entrypoint bash \
        --hostname ias0360-container \
        -v "$REPO:/root" -v "$REPO:/media/sf_repo" -w /root "$IMAGE" -c "$*"
fi
