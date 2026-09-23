"""Bypass / dry-chain analysis: latency, gain, frequency response (noise
segment, L over the reference), and where the ramp starts to compress
(input-stage / ADC clipping onset). No effect parameters."""
import numpy as np
from scipy.signal import welch
from .util import seg, cut, db


def analyze_chain(cap, fs, layout, x_signal):
    L = cap[:, 0]
    ref = cap[:, 2] if cap.shape[1] >= 3 else x_signal[: len(L)]
    out = {}
    # latency: dry click vs reference click
    s = seg(layout, "clicks")
    lats = []
    for tc in s["click_times"]:
        i = int(tc * fs)
        w = int(0.003 * fs)
        ir_ = i - w + int(np.argmax(np.abs(ref[i - w:i + w])))
        il_ = i - w + int(np.argmax(np.abs(L[i - w:i + w + int(0.002 * fs)])))
        lats.append((il_ - ir_) / fs * 1000.0)
    out["latency_ms"] = float(np.median(lats))
    # gain at 1 kHz from the sine20 segment
    s = seg(layout, "sine20")
    a, b = cut(L, fs, s["start"] + 0.5, s["end"] - 0.5), cut(ref, fs, s["start"] + 0.5, s["end"] - 0.5)
    out["gain_1k_db"] = db(np.sqrt(np.mean(a ** 2))) - db(np.sqrt(np.mean(b ** 2)))
    # response from the noise segment
    s = seg(layout, "noise")
    a, b = cut(L, fs, s["start"] + 0.3, s["end"] - 0.3), cut(ref, fs, s["start"] + 0.3, s["end"] - 0.3)
    f, Pa = welch(a, fs, nperseg=8192)
    f, Pb = welch(b, fs, nperseg=8192)
    H = 10 * np.log10(Pa / Pb)
    h1k = H[np.argmin(np.abs(f - 1000))]
    out["resp_db"] = {int(f0): float(H[np.argmin(np.abs(f - f0))] - h1k) for f0 in [30, 60, 100, 200, 500, 1000, 2000, 5000, 10000, 15000, 18000, 19000]}
    # ramp: gain per 0.1 s vs input level; onset = first point 1 dB below the low-level gain
    s = seg(layout, "ramp")
    a, b = cut(L, fs, s["start"], s["end"]), cut(ref, fs, s["start"], s["end"])
    n = int(0.1 * fs)
    rows = []
    for i in range(0, len(a) - n, n):
        pa, pb = np.abs(a[i:i + n]).max(), np.abs(b[i:i + n]).max()
        rows.append((db(pb), db(pa) - db(pb)))
    g0 = float(np.median([g for l, g in rows if l < -25]))
    onset = next((l for l, g in rows if l > -30 and g < g0 - 1.0), None)
    out["ramp_gain_low_db"] = g0
    out["ramp_onset_dbfs"] = None if onset is None else round(onset, 1)
    out["ramp_max_gain_drop_db"] = float(g0 - min(g for l, g in rows))
    out["ramp_rows"] = rows
    return out
