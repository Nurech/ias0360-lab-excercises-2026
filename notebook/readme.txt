notebook/

my scripts. leave the repo root alone, that's the course copy.

from the mac (not in the vm):
  ssh -p 2222 joosep@127.0.0.1

then paste this in the vm:

cd /media/sf_repo
./notebook/container.sh "cd lab_1_1/daq && mkdir -p build && cd build && cmake .. && make -j6"
./notebook/flash.sh lab_1_1/daq
./notebook/logs.sh 10

container.sh  build box (no usb)
flash.sh      put a .uf2 on the board
logs.sh       serial, 115200. 1200 baud = bootsel
reset.sh      run / bootsel / erase

lab0/      first-time vm + course setup
lab_1_1/   imu to sd

plug into the data usb, not the lcd one.
init the sd card before cyw43 or the led hangs.
