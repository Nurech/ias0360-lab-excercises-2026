lab 1_1

imu onto the sd card at 500 hz or more. we measure storage rate, not the sensor.
firmware lives in lab_1_1/daq/
board on the data usb, sd card in.

paste this in the vm:

cd /media/sf_repo
./notebook/container.sh "cd lab_1_1/daq && mkdir -p build && cd build && cmake .. && make -j6"
./notebook/flash.sh lab_1_1/daq
python3 notebook/lab_1_1/verify.py

verify.py does a capture and checks the bytes are real imu, not zeros.
python3 notebook/lab_1_1/capture.py   just prints the rate.

serial keys if you watch it yourself (./notebook/logs.sh):
  s start   x stop   i info   l list   d dump

last time: 10000 samples, 0 dropped, 1000 hz.
each record is t_us + ax ay az gx gy gz.
