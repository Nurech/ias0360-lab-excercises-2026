lab 3_1

two parts. firmware stays in lab_3.1/, one build dir for all three targets.
board on the data usb, lcd on.

wifi first (2.4 ghz, wpa2 psk). git ignores this file:

  lab_3.1/wifi_secrets.h
    const char WIFI_SSID[] = "...";
    const char WIFI_PASSWORD[] = "...";

paste this in the vm:

cd /media/sf_repo
./notebook/container.sh "cd lab_3.1 && mkdir -p build && cd build && cmake .. && make -j6 application1 application2 networking_main"

1. context switch

./notebook/lab_3_1/flash_apps.sh
./notebook/logs.sh 30

app1 is about 60 kb, so APP2_OFFSET 0x140000 leaves it plenty (and matches flash_program.sh).
app1 keeps boot2 at 0x10000000, its vector table is at +0x100 = APP1_OFFSET.
app2 has no boot2 and is linked straight at 0x10140000, 768 kb to the end of flash.
press SWITCH APP: "Jumping to the app2..." then the lcd says Application 2, and back.

2. networking

./notebook/flash.sh lab_3.1/build/networking_main.uf2
./notebook/logs.sh 60

one boot does it all: connect, GET healthcheck, POST greet, POST predict_a for 3.14 -1.5 0 42.
expect healthy, Hello, Raspberry Pi Pico W!, and 2x+1 for each x (7.28 for 3.14).
flashing networking_main overwrites the context switch apps; flash_apps.sh puts them back.
