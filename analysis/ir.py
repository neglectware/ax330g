"""Impulse response of the device from the 5 s log sweep, by deconvolving the
captured channel with the reference channel (or, without one, the signal
set's own sweep). A click at -16 dBFS leaves the later repeats of a delay
under the noise floor; the sweep carries ~40 dB more energy and the log
sweep puts harmonic-distortion products at negative time, clear of the
repeats. Used for the repeat spectra (feedback coefficient, High Damp).
"""
import numpy as np
from .util import seg, cut


def sweep_ir(cap, fs, layout, x_signal=None, channel=0, length_s=2.0, f_lo=20.0, f_hi=19000.0):
    """Returns (ir, i0): ir is a 1-D array covering -0.1 s .. +length_s
    around the dry arrival, i0 the index of t = 0 (the excitation's own
    time origin; the device's dry click lands a few tenths of a ms later)."""
    s = seg(layout, "sweep")
    t0, t1 = s["start"] - 0.05, s["end"] + length_s + 0.2
    y = cut(cap[:, channel], fs, t0, t1)
    if cap.shape[1] >= 3:
        x = cut(cap[:, 2], fs, t0, t1)
    else:
        x = cut(x_signal, fs, t0, t1)
    n = 1 << int(np.ceil(np.log2(len(y) + len(x))))
    Y = np.fft.rfft(y, n)
    X = np.fft.rfft(x, n)
    f = np.fft.rfftfreq(n, 1.0 / fs)
    P = np.abs(X) ** 2
    eps = 1e-3 * P[(f >= f_lo) & (f <= f_hi)].max()
    H = Y * np.conj(X) / (P + eps)
    # band-limit with a raised-cosine edge so the IR does not ring
    W = np.ones_like(f)
    lo = (f < f_lo)
    W[lo] = 0.0
    hi = (f > f_hi)
    W[hi] = 0.0
    e = (f >= f_hi * 0.9) & (f <= f_hi)
    W[e] = 0.5 + 0.5 * np.cos(np.pi * (f[e] - f_hi * 0.9) / (f_hi * 0.1))
    ir_full = np.fft.irfft(H * W, n)
    pre = int(0.1 * fs)
    post = int(length_s * fs)
    ir = np.concatenate([ir_full[-pre:], ir_full[:post]])
    return ir, pre


def ir_window(ir, i0, fs, t_ms, pre_ms=1.0, post_ms=30.0):
    """Slice of the IR around t_ms (relative to the excitation origin),
    Hann-tapered at both ends like clicks.repeat_window."""
    a = i0 + int(round((t_ms - pre_ms) * fs / 1000.0))
    b = i0 + int(round((t_ms + post_ms) * fs / 1000.0))
    a = max(a, 0)
    w = ir[a:b].copy()
    e = int(0.001 * fs)
    if len(w) > 2 * e:
        ramp = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, e))
        w[:e] *= ramp
        w[-e:] *= ramp[::-1]
    return w
