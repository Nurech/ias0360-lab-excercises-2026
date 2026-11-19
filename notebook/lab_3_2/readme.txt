lab 3_2

two firmwares in lab_3.2/. lcd/ is the one the moodle text describes,
imu/ is the newer air-writing one (upstream 15 oct). both need wifi and the sd card.
board on the data usb, sd card in.

wifi: same two lines in each, git ignores them:

  lab_3.2/lcd/wifi_secrets.h
  lab_3.2/imu/wifi_secrets.h
    const char WIFI_SSID[] = "...";
    const char WIFI_PASSWORD[] = "...";

paste this in the vm:

cd /media/sf_repo
./notebook/container.sh "cd lab_3.2/lcd && mkdir -p build && cd build && cmake .. && make -j6 main"
./notebook/container.sh "cd lab_3.2/imu && mkdir -p build && cd build && cmake .. && make -j6"

lcd (request id id_256780IAXD)

LAB_STEP in lab_3.2/lcd/main.h picks the step, rebuild after changing it:
  2  mnist vector to /api/v1/mnist, expect 8
  3  60x180 ocr image from memory, 6 chunks of 2048 to /api/v1/test_large_request
  4  same image: core 1 writes it to the sd card, then reads it back chunk by chunk,
     core 0 posts each chunk. default.

./notebook/flash.sh lab_3.2/lcd/build/main.uf2
./notebook/logs.sh 90

imu (request id 256780IAXD_<boot tag>)

the three tmp/position_*.csv are built into the firmware (position_csv.h).
after mounting, the pico writes them to the sd card, reads them back and posts
one character per packet to /api/v1/air_writing. the last reply is the word.
new csv files in tmp/? run this first, then rebuild:

  python3 notebook/lab_3_2/csv_header.py

./notebook/flash.sh lab_3.2/imu/build/main.uf2
./notebook/logs.sh 90
