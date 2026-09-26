#!/usr/bin/env python3
# no pico needed. synthetic imu now, drop a lab_1_1 .bin later.

from __future__ import annotations

import argparse
import csv
import math
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from filters import (
    axis_stats,
    dequantize,
    mcu_fft_mag,
    quantize_signed,
    snr_and_error,
    top_k_peaks,
    zscore,
)

REC = struct.Struct("<Ihhhhhh")
COUNTS_PER_G = 16384.0
FS_DEFAULT = 1000.0
N_FFT = 256


def synthetic(n=1024, fs=FS_DEFAULT):
    # 1000 hz, gravity on z, two tones so the fft has something to find.
    t, ax, ay, az = [], [], [], []
    for i in range(n):
        ti = i / fs
        t.append(ti)
        ax.append(0.04 * math.sin(2 * math.pi * 7.0 * ti))
        ay.append(0.03 * math.cos(2 * math.pi * 3.0 * ti))
        az.append(1.0 + 0.08 * math.sin(2 * math.pi * 23.0 * ti))
    return t, {"ax": ax, "ay": ay, "az": az}, fs, "synthetic"


def load_bin(path, fs=None):
    raw = Path(path).read_bytes()
    n = len(raw) // REC.size
    t_us, axes = [], {"ax": [], "ay": [], "az": []}
    for i in range(n):
        rec = REC.unpack_from(raw, i * REC.size)
        t_us.append(rec[0])
        axes["ax"].append(rec[1] / COUNTS_PER_G)
        axes["ay"].append(rec[2] / COUNTS_PER_G)
        axes["az"].append(rec[3] / COUNTS_PER_G)
    if len(t_us) < 2:
        sys.exit(f"not enough records in {path}")
    span = (t_us[-1] - t_us[0]) / 1e6
    fs_est = (len(t_us) - 1) / span if span > 0 else FS_DEFAULT
    t = [(u - t_us[0]) / 1e6 for u in t_us]
    return t, axes, (fs if fs else fs_est), str(path)


def write_csv(path, rows, header):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)


def maybe_plot(out, t, axes, fs, freqs, mag_mcu, mag_np, name):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import numpy as np
    except ImportError:
        print("matplotlib/numpy missing, skipped plots")
        return

    fig, ax = plt.subplots(figsize=(8, 3))
    for k, c in (("ax", "C0"), ("ay", "C1"), ("az", "C2")):
        ax.plot(t[: len(axes[k])], axes[k], label=k, color=c, lw=0.8)
    ax.set_title(f"time ({name}, fs={fs:.1f} hz)")
    ax.set_xlabel("s")
    ax.set_ylabel("g")
    ax.legend()
    fig.tight_layout()
    fig.savefig(out / "imu_signal.png", dpi=140)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(8, 3))
    ax.plot(freqs, mag_mcu, label="mcu-shaped fft", lw=1.0)
    ax.plot(freqs, mag_np, label="numpy rfft", lw=1.0, alpha=0.75)
    ax.set_title("fft overlay (az, hamming)")
    ax.set_xlabel("hz")
    ax.set_ylabel("amp")
    ax.set_xlim(0, fs / 2)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out / "imu_fft_spectrum.png", dpi=140)
    plt.close(fig)
    print(f"  plots: {out / 'imu_signal.png'}")
    print(f"         {out / 'imu_fft_spectrum.png'}")


def numpy_fft_mag(x, fs):
    import numpy as np
    n = len(x)
    w = np.hamming(n)
    xw = np.asarray(x, dtype=float) * w
    mag = np.abs(np.fft.rfft(xw, n=n))
    scale = (2.0 / n) / 0.54
    amp = mag * scale
    freqs = np.fft.rfftfreq(n, 1.0 / fs)
    return freqs.tolist(), amp.tolist()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", help="lab_1_1 daq .bin, 16-byte records")
    ap.add_argument("--fs", type=float, default=None)
    ap.add_argument("--out", default=str(HERE / "out"))
    a = ap.parse_args()

    if a.bin:
        t, axes, fs, name = load_bin(a.bin, a.fs)
    else:
        t, axes, fs, name = synthetic(fs=a.fs or FS_DEFAULT)
        print("no .bin yet, using synthetic imu (1000 hz, gravity on z + two tones)")

    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    print(f"source={name}  n={len(t)}  fs={fs:.2f} hz\n")

    print("=== 1 quantization (az scaled to [-1,1)) ===")
    az = axes["az"]
    peak = max(abs(v) for v in az) or 1.0
    az_n = [v / (peak * 1.01) for v in az]
    qrows = []
    for bits in (16, 8, 4):
        q, clips, scale = quantize_signed(az_n, bits)
        xq = dequantize(q, scale)
        snr, maxe, rms = snr_and_error(az_n, xq)
        bytes_f32 = 4 * len(az_n)
        bytes_q = max(1, bits) * len(az_n) // 8
        print(f"  q{bits-1}: snr={snr:.2f} dB  rms={rms:.6g}  max|e|={maxe:.6g}  "
              f"clips={clips}  bytes {bytes_f32}->{bytes_q}")
        qrows.append((bits, snr, rms, maxe, clips, bytes_f32, bytes_q))
        write_csv(out / f"az_q{bits}.csv",
                  [(i, az_n[i], q[i], xq[i]) for i in range(len(az_n))],
                  ["i", "float_norm", "q", "dequant"])
    write_csv(out / "quant_summary.csv", qrows,
              ["bits", "snr_db", "rms", "max_abs", "clips", "bytes_f32", "bytes_q"])

    print("\n=== 2 statistics (raw g, then z-score) ===")
    stat_rows = []
    for axis, x in axes.items():
        s = axis_stats(x)
        zn = axis_stats(zscore(x))
        print(f"  {axis}: mean={s['mean']:+.5f}  med={s['median']:+.5f}  "
              f"std={s['std']:.5f}  min={s['min']:+.5f}  max={s['max']:+.5f}")
        if s["mode_n"] > 1:
            print(f"        mode={s['mode']:+.5f} (n={s['mode_n']})")
        stat_rows.append((axis, "raw", s["mean"], s["median"], s["var"], s["std"], s["min"], s["max"]))
        stat_rows.append((axis, "z", zn["mean"], zn["median"], zn["var"], zn["std"], zn["min"], zn["max"]))
    write_csv(out / "stats.csv", stat_rows,
              ["axis", "scale", "mean", "median", "var", "std", "min", "max"])

    print("\n=== 3 fft (az, first 256 samples, dc removed) ===")
    block = az[:N_FFT]
    if len(block) < N_FFT:
        sys.exit(f"need {N_FFT} samples for fft, have {len(block)}")
    mean_az = sum(block) / len(block)
    block = [v - mean_az for v in block]
    freqs, mag_mcu, windowed = mcu_fft_mag(block, fs)
    peaks = top_k_peaks(mag_mcu, 5)
    print("  mcu-shaped top 5 (excludes dc):")
    for i, b in enumerate(peaks, 1):
        print(f"    {i}: bin={b}  freq={freqs[b]:.2f} hz  amp={mag_mcu[b]:.4f}")

    (out / "raw_imu_mcu.txt").write_text(",".join(f"{v:.6f}" for v in windowed) + "\n")
    with (out / "out_fft_mcu.txt").open("w") as f:
        f.write("bin,freq,amp\n")
        for b in peaks:
            f.write(f"{b},{freqs[b]:.6f},{mag_mcu[b]:.6f}\n")

    try:
        freqs_np, mag_np = numpy_fft_mag(block, fs)
        print("  numpy top 5:")
        order = sorted(range(1, len(mag_np)), key=lambda i: mag_np[i], reverse=True)[:5]
        for i, b in enumerate(order, 1):
            print(f"    {i}: bin={b}  freq={freqs_np[b]:.2f} hz  amp={mag_np[b]:.4f}")
        # same bins: how close is the c-shaped fft to numpy
        err = [abs(mag_mcu[k] - mag_np[k]) for k in range(len(mag_mcu))]
        print(f"  |mcu-numpy| mean={sum(err)/len(err):.4g}  max={max(err):.4g}")
    except ImportError:
        mag_np = mag_mcu
        freqs_np = freqs
        print("  numpy not installed, skipped pc fft")

    maybe_plot(out, t, axes, fs, freqs, mag_mcu, mag_np, name)
    print(f"\nout -> {out}")
    print("placeholder only. pico path later: flash lab_1_2, dump these same files from sd.")


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
