#!/usr/bin/env python3
# send s, wait for DONE, print the rate
import argparse
import os
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("needs pyserial: sudo apt install -y python3-serial")

DONE = re.compile(
    r"DONE file=(\S+) samples=(\d+) dropped=(\d+) seconds=([\d.]+) rate=([\d.]+)"
)


def main():
    def pick_port():
        for p in (os.environ.get("PICO_PORT"), "/dev/pico", "/dev/ttyACM0"):
            if p and os.path.exists(p):
                return p
        return "/dev/pico"

    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=pick_port())
    ap.add_argument("--wait", type=float, default=40.0)
    a = ap.parse_args()

    try:
        port = serial.Serial(a.port, 115200, timeout=0.3)
    except Exception as e:
        sys.exit(f"could not open {a.port}: {e}")

    with port:
        time.sleep(0.5)
        port.reset_input_buffer()
        print(f"starting a capture on {a.port} ...")
        port.write(b"s")
        port.flush()

        end, buf = time.time() + a.wait, ""
        while time.time() < end:
            chunk = port.read(4096)
            if chunk:
                buf += chunk.decode(errors="replace")
                if "DONE" in buf:
                    break

    for line in buf.splitlines():
        line = line.strip()
        # the firmware echoes received bytes as "RX nnn"; not interesting here
        if line and not line.startswith("RX "):
            print("  " + line)

    m = DONE.search(buf)
    if not m:
        sys.exit("\nno DONE line within the timeout - the capture did not finish")

    fname, samples, dropped, seconds, rate = m.groups()
    rate_f = float(rate)
    passed = rate_f >= 500.0 and dropped == "0"

    bar = "=" * 54
    print(f"\n{bar}")
    print(f"  measured storage rate : {rate_f:.1f} Hz")
    print(f"  requirement           : 500 Hz (rate of storage on the SD card)")
    print(f"  dropped samples       : {dropped}")
    print(f"  samples / duration    : {samples} in {seconds} s")
    print(f"  file on the card      : {fname}")
    print(f"  VERDICT               : {'PASS' if passed else 'FAIL'}")
    print(bar)
    return 0 if passed else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
