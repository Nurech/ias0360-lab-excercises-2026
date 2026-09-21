#!/usr/bin/env bash
# Once, inside the VM: bash /media/sf_repo/notebook/lab0/vm-setup.sh
set -euo pipefail

log()  { printf '\n\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m %s\n' "$*" >&2; }

SHARED=/media/sf_repo          # the Mac's repo, same files, live both ways

# ---------------------------------------------------------------- sanity
log "checking this is the ARM64 Ubuntu guest"
[ "$(uname -m)" = "aarch64" ] || { warn "expected aarch64, got $(uname -m)"; exit 1; }
. /etc/os-release && echo "    $PRETTY_NAME on $(uname -m)"

# ---------------------------------------------------------------- base packages
log "installing the build toolchain and utilities"
sudo apt-get update -qq
sudo apt-get install -y -qq \
    git curl ca-certificates build-essential cmake ninja-build \
    gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
    python3 python3-pip python3-venv python3-serial \
    minicom picocom usbutils rsync openssh-server

# ---------------------------------------------------------------- ssh in from the Mac
log "enabling SSH so the Mac can run commands in here"
sudo systemctl enable --now ssh
mkdir -p "$HOME/.ssh" && chmod 700 "$HOME/.ssh"
if [ -f "$SHARED/notebook/lab0/.vm_authorized_key" ]; then
    # public half only; the private key never leaves the Mac
    grep -qxF "$(cat "$SHARED/notebook/lab0/.vm_authorized_key")" "$HOME/.ssh/authorized_keys" 2>/dev/null \
        || cat "$SHARED/notebook/lab0/.vm_authorized_key" >> "$HOME/.ssh/authorized_keys"
    chmod 600 "$HOME/.ssh/authorized_keys"
    echo "    authorised the Mac's key for passwordless ssh"
else
    warn "no key at $SHARED/notebook/lab0/.vm_authorized_key - ssh will ask for your password"
fi

# ---------------------------------------------------------------- docker
log "installing Docker and adding you to the docker group"
sudo apt-get install -y -qq docker.io
sudo systemctl enable --now docker
sudo usermod -aG docker "$USER"

# ---------------------------------------------------------------- the Pico over USB
log "adding the udev rule so the Pico is usable without sudo"
sudo tee /etc/udev/rules.d/99-pico.rules >/dev/null <<'RULES'
SUBSYSTEM=="usb", ATTRS{idVendor}=="2e8a", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="tty", ATTRS{idVendor}=="2e8a", MODE="0666", GROUP="dialout", SYMLINK+="pico"
RULES
sudo groupadd -f plugdev
sudo usermod -aG plugdev,dialout "$USER"
sudo udevadm control --reload-rules && sudo udevadm trigger

# ---------------------------------------------------------------- shared folder
log "granting access to the shared folder from the Mac"
sudo usermod -aG vboxsf "$USER" || warn "no vboxsf group - install Guest Additions, then rerun this"
if [ -d "$SHARED" ]; then
    echo "    $SHARED is mounted"
else
    warn "$SHARED is not mounted yet. Install Guest Additions (Devices > Insert Guest"
    warn "Additions CD image), reboot, then rerun this script."
fi

# ---------------------------------------------------------------- VS Code
log "installing VS Code (arm64)"
if ! command -v code >/dev/null; then
    tmp=$(mktemp -d)
    curl -fsSL "https://code.visualstudio.com/sha/download?build=stable&os=linux-deb-arm64" \
         -o "$tmp/code.deb"
    sudo apt-get install -y -qq "$tmp/code.deb"
    rm -rf "$tmp"
else
    echo "    already installed"
fi

# ---------------------------------------------------------------- the course image
log "building the course Docker image"
if [ -d "$SHARED/own_pc_setup" ]; then
    sudo docker build -t ias0360-2026 "$SHARED/own_pc_setup"
else
    warn "own_pc_setup/ not reachable, skipping the image build"
fi

# ---------------------------------------------------------------- done
log "done"
cat <<EOF

  Log out and back in once (docker / plugdev / dialout / vboxsf).

  Code: $SHARED
  Board: lsusb | grep 2e8a
  Then paste, from $SHARED:
    ./notebook/container.sh "cd blink_example && mkdir -p build && cd build && cmake .. && make -j6"
    ./notebook/flash.sh blink_example
    ./notebook/logs.sh 10
  From the Mac: ssh -p 2222 \$USER@127.0.0.1
EOF
