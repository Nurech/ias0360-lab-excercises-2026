#!/usr/bin/env python3
# run a capture, dump the file, check it's real imu data
import argparse
import os
import re
import statistics
import struct
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("needs pyserial: sudo apt install -y python3-serial")

REC = struct.Struct("<Ihhhhhh")          # t_us, ax, ay, az, gx, gy, gz  = 16 bytes
DONE = re.compile(r"DONE file=(\S+) samples=(\d+) dropped=(\d+) seconds=([\d.]+) rate=([\d.]+)")
DUMP_BEGIN = re.compile(r"DUMP begin file=(\S+) bytes=(\d+) record=(\d+)")
COUNTS_PER_G = 16384                     # +-2 g full scale on the ICM-20948


def talk(port, cmd, until, timeout):
    port.reset_input_buffer()
    port.write(cmd)
    port.flush()
    end, buf = time.time() + timeout, ""
    while time.time() < end:
        chunk = port.read(65536)
        if chunk:
            buf += chunk.decode(errors="replace")
            if until in buf:
                break
    return buf


def main():
    def pick_port():
        for p in (os.environ.get("PICO_PORT"), "/dev/pico", "/dev/ttyACM0"):
            if p and os.path.exists(p):
                return p
        return "/dev/pico"

    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=pick_port())
    a = ap.parse_args()

    with serial.Serial(a.port, 115200, timeout=0.5) as port:
        time.sleep(0.5)

        print("1. running a capture ...")
        cap = talk(port, b"s", "DONE", 45)
        m = DONE.search(cap)
        if not m:
            sys.exit("   no DONE line - the capture did not finish")
        fname, n_rep, dropped, secs, rate_rep = m.groups()
        print(f"   firmware reports: {n_rep} samples, {dropped} dropped, "
              f"{secs}s, {float(rate_rep):.1f} Hz")

        print("2. dumping the file back over serial (this takes a moment) ...")
        dump = talk(port, b"d", "DUMP end", 180)

    b = DUMP_BEGIN.search(dump)
    if not b:
        sys.exit("   no DUMP begin line - is the firmware current?")
    dfile, dbytes, drec = b.groups()
    print(f"   {dfile}, {dbytes} bytes, {drec} B per record")

    body = dump[dump.index("DUMP begin"):]
    body = body[body.index("\n") + 1: body.index("DUMP end")]
    hexstr = "".join(c for c in body if c in "0123456789abcdefABCDEF")
    raw = bytes.fromhex(hexstr[: len(hexstr) // 2 * 2])
    print(f"   decoded {len(raw)} bytes")

    n = len(raw) // REC.size
    if n == 0:
        sys.exit("   nothing decoded")
    recs = [REC.unpack_from(raw, i * REC.size) for i in range(n)]
    t = [r[0] for r in recs]
    ax = [r[1] for r in recs]
    ay = [r[2] for r in recs]
    az = [r[3] for r in recs]

    print(f"\n3. checking {n} records\n")
    print("   first 3 records (t_us, ax, ay, az, gx, gy, gz):")
    for r in recs[:3]:
        print(f"     {r}")

    # --- is it real data, or zeros / a constant? -------------------------------
    nonzero = sum(1 for r in recs if any(v != 0 for v in r[1:]))
    distinct_az = len(set(az))
    print(f"\n   non-zero samples   : {nonzero}/{n}")
    print(f"   distinct az values : {distinct_az}  (1 would mean a frozen constant)")

    # --- does gravity read right? ---------------------------------------------
    mean_az, mean_ax, mean_ay = statistics.mean(az), statistics.mean(ax), statistics.mean(ay)
    g = mean_az / COUNTS_PER_G
    print(f"\n   mean ax {mean_ax:9.1f}   ay {mean_ay:9.1f}   az {mean_az:9.1f}")
    print(f"   az in g            : {g:.3f} g   (flat board at rest should be near 1.000)")

    # --- do the timestamps agree with the reported rate? ----------------------
    span_s = (t[-1] - t[0]) / 1e6
    rate_ts = (n - 1) / span_s if span_s > 0 else 0
    deltas = [t[i + 1] - t[i] for i in range(n - 1)]
    print(f"\n   timestamp span     : {span_s:.3f} s over {n} samples")
    print(f"   rate from t_us     : {rate_ts:.1f} Hz")
    print(f"   rate reported      : {float(rate_rep):.1f} Hz")
    print(f"   sample interval    : median {statistics.median(deltas):.0f} us, "
          f"min {min(deltas)}, max {max(deltas)}")

    # --- verdict ---------------------------------------------------------------
    checks = {
        "data is not all zeros": nonzero > n * 0.99,
        "az is not a frozen constant": distinct_az > 10,
        "gravity reads 0.8-1.2 g on Z": 0.8 <= g <= 1.2,
        "x and y are small vs z": abs(mean_ax) < abs(mean_az) / 3 and abs(mean_ay) < abs(mean_az) / 3,
        "timestamp rate matches reported": abs(rate_ts - float(rate_rep)) / float(rate_rep) < 0.05,
        "meets the 500 Hz requirement": rate_ts >= 500.0,
        "no samples dropped": dropped == "0",
    }
    print("\n" + "=" * 56)
    for label, ok in checks.items():
        print(f"   [{'PASS' if ok else 'FAIL'}]  {label}")
    allok = all(checks.values())
    print("=" * 56)
    print(f"   LAB 1_1: {'VERIFIED' if allok else 'NOT VERIFIED'}")
    print("=" * 56)
    return 0 if allok else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
