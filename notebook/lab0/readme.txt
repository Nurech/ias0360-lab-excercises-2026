lab 0

mac edits, ubuntu vm builds and talks usb, pico runs the .uf2.
mac docker can't see the board, that's why the vm exists.
/media/sf_repo in the vm is this folder on the mac.

flashing is copying a .uf2 onto a fake usb stick.
bootsel = usb disk, no led, no serial.
running  = /dev/pico, your printf. that's the debugger.
firmware without usb stdio looks dead. hold bootsel while plugging in.
don't open serial at 1200 baud, that sends it back to bootsel.
plug into the data usb, not the lcd one.

github key and forum line are already done. don't add the key again.

first time, paste this in the vm, then log out and back in:

bash /media/sf_repo/notebook/lab0/vm-setup.sh

after that, paste this:

cd /media/sf_repo
docker info
docker image inspect ias0360-2026 >/dev/null 2>&1 || docker build -t ias0360-2026 /media/sf_repo/own_pc_setup
[ -f ~/.ssh/id_ed25519_ias0360 ] || ssh-keygen -t ed25519 -N "" -C "ias0360" -f ~/.ssh/id_ed25519_ias0360
chmod 600 ~/.ssh/id_ed25519_ias0360
cat ~/.ssh/id_ed25519_ias0360.pub
ssh -i ~/.ssh/id_ed25519_ias0360 -o IdentitiesOnly=yes -T git@github.com || true
git remote get-url upstream >/dev/null 2>&1 || git remote add upstream https://github.com/taltech-eailab-courses/ias0360-lab-excercises-2026.git
git fetch upstream
./notebook/container.sh "cd blink_example && mkdir -p build && cd build && cmake .. && make -j6"
./notebook/flash.sh blink_example
./notebook/logs.sh 10

dark led on a pico w is ok, that led is on the wifi chip.
no /dev/pico = vbox usb filter 2e8a isn't holding the board.
builds owned by root = share is not uid=1000.
