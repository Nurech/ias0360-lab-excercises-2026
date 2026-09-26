# same math as lab_1_2/*.c, so we can run it on the mac without the pico.
# later the pico just dumps the same numbers and we overlay the plots.

from __future__ import annotations

import math


def clampf(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def quantize_signed(x, bits):
    # bits 16 / 8 / 4. course c file only shows q15; same idea, smaller scale.
    scale = float(1 << (bits - 1))
    qmax = (1 << (bits - 1)) - 1
    qmin = -(1 << (bits - 1))
    hi = (qmax + 0.0) / scale
    clips = 0
    q = []
    for v in x:
        s = clampf(v, -hi, hi)
        if s != v:
            clips += 1
        qi = int(round(s * scale))
        if qi > qmax:
            qi = qmax
        if qi < qmin:
            qi = qmin
        q.append(qi)
    return q, clips, scale


def dequantize(q, scale):
    return [qi / scale for qi in q]


def snr_and_error(ref, test):
    n = len(ref)
    sig = 0.0
    err = 0.0
    maxe = 0.0
    for a, b in zip(ref, test):
        e = a - b
        sig += a * a
        err += e * e
        ae = abs(e)
        if ae > maxe:
            maxe = ae
    rms = math.sqrt(err / n)
    snr = math.inf if err == 0.0 else 10.0 * math.log10(sig / err)
    return snr, maxe, rms


def mean_f32(x):
    return sum(x) / len(x)


def variance_f32(x, mean):
    n = len(x)
    if n <= 1:
        return 0.0
    acc = 0.0
    for v in x:
        d = v - mean
        acc += d * d
    return acc / (n - 1)


def min_max_f32(x):
    return min(x), max(x)


def median_f32(x):
    tmp = sorted(x)
    n = len(tmp)
    if n & 1:
        return tmp[n // 2]
    return 0.5 * (tmp[n // 2 - 1] + tmp[n // 2])


def mode_f32(x, eps=0.0):
    best_count = 0
    best_val = None
    n = len(x)
    for i, xi in enumerate(x):
        cnt = 1
        for j in range(i + 1, n):
            if abs(x[j] - xi) <= eps:
                cnt += 1
        if cnt > best_count:
            best_count = cnt
            best_val = xi
    return best_val, best_count


def axis_stats(x):
    m = mean_f32(x)
    var = variance_f32(x, m)
    lo, hi = min_max_f32(x)
    mode, mode_n = mode_f32(x)
    return {
        "mean": m,
        "median": median_f32(x),
        "var": var,
        "std": math.sqrt(var),
        "min": lo,
        "max": hi,
        "mode": mode,
        "mode_n": mode_n,
    }


def zscore(x):
    m = mean_f32(x)
    s = math.sqrt(variance_f32(x, m))
    if s == 0.0:
        return [0.0] * len(x)
    return [(v - m) / s for v in x]


def hamming_window(x):
    n = len(x)
    out = list(x)
    for i in range(n):
        out[i] *= 0.54 - 0.46 * math.cos(2.0 * math.pi * i / (n - 1))
    return out


def _reverse_bits(v, nbits):
    r = 0
    for _ in range(nbits):
        r = (r << 1) | (v & 1)
        v >>= 1
    return r


def fft_radix2(re, im, direction=1):
    # in-place, same recurrence as lab_1_2/fft.c
    n = len(re)
    if n <= 0 or (n & (n - 1)) != 0:
        raise ValueError("n must be a power of two")
    logn = int(math.log2(n))
    for i in range(n):
        j = _reverse_bits(i, logn)
        if j > i:
            re[i], re[j] = re[j], re[i]
            im[i], im[j] = im[j], im[i]

    sgn = -1.0 if direction >= 0 else 1.0
    for s in range(1, logn + 1):
        m = 1 << s
        m2 = m >> 1
        theta = sgn * math.pi / m2
        wpr = -2.0 * math.sin(0.5 * theta) * math.sin(0.5 * theta)
        wpi = math.sin(theta)
        for k in range(0, n, m):
            wr, wi = 1.0, 0.0
            for j in range(m2):
                t = k + j + m2
                u = k + j
                tr = wr * re[t] - wi * im[t]
                ti = wr * im[t] + wi * re[t]
                ur, ui = re[u], im[u]
                re[t] = ur - tr
                im[t] = ui - ti
                re[u] = ur + tr
                im[u] = ui + ti
                tmp = wr
                wr = wr + (wr * wpr - wi * wpi)
                wi = wi + (wi * wpr + tmp * wpi)

    if direction < 0:
        inv = 1.0 / n
        for i in range(n):
            re[i] *= inv
            im[i] *= inv
    return re, im


def mcu_fft_mag(x, fs):
    n = len(x)
    windowed = hamming_window(x)
    re = list(windowed)
    im = [0.0] * n
    fft_radix2(re, im, +1)
    mag = [math.sqrt(re[i] * re[i] + im[i] * im[i]) for i in range(n)]
    scale = (2.0 / n) / 0.54
    half = n // 2 + 1
    mag = [mag[k] * scale for k in range(half)]
    df = fs / n
    freqs = [df * k for k in range(half)]
    return freqs, mag, windowed


def top_k_peaks(mag, k=5, exclude_dc=True):
    start = 1 if exclude_dc else 0
    taken = []
    for _ in range(k):
        best_i, best_v = -1, -1.0
        for i in range(start, len(mag)):
            if i in taken:
                continue
            if mag[i] > best_v:
                best_v = mag[i]
                best_i = i
        if best_i >= 0:
            taken.append(best_i)
    return taken
