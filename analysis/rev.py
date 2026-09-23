"""Reverb (REV) analysis helpers: load/align a TAIL-set capture, remove the
measured converter chain, and pull the early-echo structure, decay slopes and
spectral decay out of the click / burst / tone tails.

The captures carry the unit's converter chain (models/ax30g-chain.json), so a
raw click response is the reverb's impulse response convolved with a ~7 ms
FIR plus three 5.49 Hz high-passes. Everything here that needs timing or echo
amplitude works on the chain-deconvolved response; everything that needs only
a decay slope can use the raw one.
"""
import os
import sys
import json
from fractions import Fraction
import numpy as np
from scipy.signal import resample_poly, fftconvolve, sosfilt, sosfiltfilt, butter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)

from analysis.util import load_wav, load_layout, seg, cut  # noqa: E402
from analysis.align import align_capture  # noqa: E402
from engine.render import chain_fir, _hpf1_sos  # noqa: E402

FS_DEV = 39062.5
CAP_DIR = os.path.join(ROOT, "captures")
SIGNAL = os.path.join(ROOT, "capture", "signalset-tail.wav")
LAYOUT = os.path.join(ROOT, "capture", "layout-tail.json")
CHAIN = os.path.join(ROOT, "models", "ax30g-chain.json")


def rev_name(**kw):
    """Capture file name from grid parameters."""
    return ("AX30G_REV_HighDamp-{HighDamp}_Balance-{Balance}_Type-{Type}"
            "_PreDly-{PreDly}_RevTime-{RevTime}_IN-LIN_SET-tail.wav").format(**kw)


_cache = {}


def load_rev(path, fs_work=48000.0):
    """Aligned (N,3) capture at fs_work: L, R, reference. Cached."""
    key = (path, fs_work)
    if key in _cache:
        return _cache[key]
    if not os.path.isabs(path) and not os.path.exists(path):
        path = os.path.join(CAP_DIR, path)
    x, fs_sig = load_wav(SIGNAL)
    x = x[:, 0]
    layout = load_layout(LAYOUT)
    cap, fs = load_wav(path)
    if fs != fs_sig:
        fr = Fraction(int(fs_sig), int(fs))
        cap = resample_poly(cap, fr.numerator, fr.denominator, axis=0)
        fs = fs_sig
    y, info = align_capture(cap, fs, x, layout)
    if fs_work != fs:
        fr = Fraction(fs_work / fs).limit_denominator(4096)
        y = resample_poly(y, fr.numerator, fr.denominator, axis=0)
    _cache[key] = (y, float(fs_work), info, layout)
    return _cache[key]


def chain_spec():
    with open(CHAIN) as f:
        return json.load(f)


def deconv_chain(y, fs, chain=None, reg_db=-60.0, lf=False, hp_hz=30.0):
    """Remove the measured converter chain from a captured signal.

    The LF high-pass cascade is inverted exactly (it is a known IIR run
    backwards would be unstable, so it is inverted as its reciprocal IIR,
    which is a leaky integrator cascade -- stable, but it re-injects DC
    drift; a 3 Hz high-pass is applied afterwards to keep it bounded).
    The HF FIR is inverted by regularised spectral division.
    """
    chain = chain or chain_spec()
    out = np.asarray(y, dtype=float)
    single = out.ndim == 1
    if single:
        out = out[:, None]
    if chain.get("hf"):
        h, pre = chain_fir(chain, fs)
        n = 1
        while n < len(out) + len(h):
            n *= 2
        H = np.fft.rfft(h, n)
        mag = np.abs(H)
        eps = (10.0 ** (reg_db / 20.0)) * mag.max()
        Hinv = np.conj(H) / (mag ** 2 + eps ** 2)
        cols = []
        for c in range(out.shape[1]):
            Y = np.fft.rfft(out[:, c], n)
            z = np.fft.irfft(Y * Hinv, n)
            cols.append(np.roll(z, pre)[: out.shape[0]])
        out = np.stack(cols, axis=1)
    hz = (chain.get("lf_hpf_hz") or []) if lf else []
    if hz:
        # inverse of each first-order high-pass: swap b and a
        for v in hz:
            s = _hpf1_sos(fs, float(v))
            b = np.array([s[0], s[1], s[2]])
            a = np.array([s[3], s[4], s[5]])
            inv = np.concatenate([a / b[0], b / b[0]])[None, :]
            out = sosfilt(inv, out, axis=0)
    if hp_hz:
        sos = butter(2, hp_hz / (fs / 2), btype="highpass", output="sos")
        out = sosfilt(sos, out, axis=0)
    return out[:, 0] if single else out


def tail(y, fs, layout, name="clicks", pre_s=0.01, dur_s=None):
    """Cut one tail segment; returns (t0_index, array). t=0 of the excitation
    sits at index int(pre_s*fs)."""
    s = seg(layout, name)
    t0 = s.get("click_times", s.get("burst_times", [s["start"]]))[0]
    a = int(round((t0 - pre_s) * fs))
    b = int(round((s["end"] if dur_s is None else t0 + dur_s) * fs))
    return int(round(pre_s * fs)), y[a:b]


def to_device_samples(t_s):
    return t_s * FS_DEV


def device_ir(path, segment="clicks", dur_s=0.3, pre_s=0.01, reg_db=-55.0,
              fs_work=48000.0, channels=(0, 1), bl_hz=(14000.0, 18500.0)):
    """Impulse response of the DSP at the device rate, from a tail-set capture.

    capture = chain( up( DEV( down(x) ) ) ), so deconvolving the measured
    chain and resampling to 39062.5 Hz gives DEV(down(x)); dividing out
    down(x) (the excitation on the same grid) leaves DEV's own impulse
    response. Returns (i0, h) with h (N, len(channels)) and the excitation's
    t=0 at index i0.
    """
    y, fs, info, layout = load_rev(path, fs_work)
    x, fs_sig = load_wav(SIGNAL)
    x = x[:, 0]
    if fs_sig != fs:
        fr = Fraction(fs / fs_sig).limit_denominator(4096)
        x = resample_poly(x, fr.numerator, fr.denominator)
    i0, cap = tail(y, fs, layout, segment, pre_s=pre_s, dur_s=dur_s)
    s = seg(layout, segment)
    t0 = s.get("click_times", s.get("burst_times", [s["start"]]))[0]
    a = int(round((t0 - pre_s) * fs))
    exc = x[a: a + cap.shape[0]]
    z = deconv_chain(cap[:, list(channels)], fs)
    fr = Fraction(FS_DEV / fs).limit_denominator(4096)
    r = resample_poly(z, fr.numerator, fr.denominator, axis=0)
    e = resample_poly(exc, fr.numerator, fr.denominator)
    n = 1
    while n < len(r) * 2:
        n *= 2
    E = np.fft.rfft(e, n)
    mag = np.abs(E)
    eps = (10.0 ** (reg_db / 20.0)) * mag.max()
    Einv = np.conj(E) / (mag ** 2 + eps ** 2)
    if bl_hz:
        fq = np.fft.rfftfreq(n, 1.0 / FS_DEV)
        lo, hi = bl_hz
        w = np.clip((fq - lo) / (hi - lo), 0.0, 1.0)
        Einv *= 0.5 + 0.5 * np.cos(np.pi * w)
    # dividing out E removes the excitation's own position as well, so the
    # result's t=0 sits at index 0; roll it back to i0d so the pre-excitation
    # window is still there as a noise-floor reference.
    i0d = int(round(pre_s * FS_DEV))
    cols = [np.roll(np.fft.irfft(np.fft.rfft(r[:, c], n) * Einv, n), i0d)[: len(r)]
            for c in range(r.shape[1])]
    h = np.stack(cols, axis=1)
    return i0d, h, info


def octave_bands(x, fs, centers=(125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0)):
    """Return {fc: filtered signal} with 4th-order Butterworth octave bands
    (zero-phase, so the decay is not skewed by the filter's own ringing)."""
    from scipy.signal import butter, sosfiltfilt
    out = {}
    for fc in centers:
        lo, hi = fc / np.sqrt(2.0), fc * np.sqrt(2.0)
        hi = min(hi, 0.45 * fs)
        if lo >= hi:
            continue
        sos = butter(4, [lo / (fs / 2), hi / (fs / 2)], btype="bandpass", output="sos")
        out[fc] = sosfiltfilt(sos, x)
    return out


def edc(x, noise_tail_frac=0.1):
    """Schroeder backward integration in dB, with Lundeby-style noise
    subtraction: the mean square of the last `noise_tail_frac` of the signal
    is treated as stationary noise and removed before integrating."""
    x = np.asarray(x, dtype=float)
    n = len(x)
    nn = max(int(n * noise_tail_frac), 16)
    noise = float(np.mean(x[-nn:] ** 2))
    p = x ** 2 - noise
    e = np.cumsum(p[::-1])[::-1]
    e = np.maximum(e, 1e-30)
    return 10 * np.log10(e / e[0])


def decay_slope(x, fs, lo_db=-5.0, hi_db=-25.0, t_skip=0.0):
    """Least-squares slope (dB/s) of the Schroeder curve between lo_db and
    hi_db, and the RT60 it implies. Returns (rt60_s, slope_db_per_s, r2, span_s)."""
    c = edc(x)
    t = np.arange(len(c)) / fs
    m = (c <= lo_db) & (c >= hi_db) & (t >= t_skip)
    if m.sum() < 32:
        return float("nan"), float("nan"), float("nan"), 0.0
    tt, cc = t[m], c[m]
    A = np.vstack([tt, np.ones_like(tt)]).T
    sol, *_ = np.linalg.lstsq(A, cc, rcond=None)
    slope = float(sol[0])
    pred = A @ sol
    ss = 1 - float(np.sum((cc - pred) ** 2) / max(np.sum((cc - cc.mean()) ** 2), 1e-30))
    return -60.0 / slope, slope, ss, float(tt[-1] - tt[0])


def frac_shift(x, lag):
    """Shift x by +lag samples (fractional) with an FFT phase ramp."""
    n = 1
    while n < len(x) * 2:
        n *= 2
    X = np.fft.rfft(x, n)
    f = np.fft.rfftfreq(n)
    return np.fft.irfft(X * np.exp(-2j * np.pi * f * lag), n)[: len(x)]


def best_shift(a, b):
    """Parabolic-interpolated lag that aligns b onto a."""
    from scipy.signal import correlate
    c = correlate(a, b, mode="full")
    k = int(np.argmax(np.abs(c)))
    if 0 < k < len(c) - 1:
        y0, y1, y2 = abs(c[k - 1]), abs(c[k]), abs(c[k + 1])
        den = y0 - 2 * y1 + y2
        frac = 0.5 * (y0 - y2) / den if den else 0.0
    else:
        frac = 0.0
    return (k - (len(b) - 1)) + frac


def null_pair(a, b, span=1.5, iters=40):
    """Align b onto a (fractional lag + one gain) and return
    (null_db, lag, gain, residual). null_db is 20log10(rms(resid)/rms(a))."""
    lag0 = best_shift(a, b)

    def cost(lag):
        m = frac_shift(b, lag)
        g = np.dot(m, a) / max(np.dot(m, m), 1e-30)
        return float(np.sum((a - g * m) ** 2)), g, m

    lo, hi = lag0 - span, lag0 + span
    gr = (np.sqrt(5) - 1) / 2
    c1, c2 = hi - gr * (hi - lo), lo + gr * (hi - lo)
    f1, f2 = cost(c1)[0], cost(c2)[0]
    for _ in range(iters):
        if f1 < f2:
            hi, c2, f2 = c2, c1, f1
            c1 = hi - gr * (hi - lo)
            f1 = cost(c1)[0]
        else:
            lo, c1, f1 = c1, c2, f2
            c2 = lo + gr * (hi - lo)
            f2 = cost(c2)[0]
    lag = 0.5 * (lo + hi)
    e, g, m = cost(lag)
    r = a - g * m
    null = 20 * np.log10(np.sqrt(np.mean(r ** 2)) / max(np.sqrt(np.mean(a ** 2)), 1e-30))
    return null, lag, g, r


def analyze_rev(path, outdir=None):
    """Bench-side REV analysis: decay of the click, -20 dBFS burst and
    -6 dBFS burst tails, per-octave RT60 of the click tail, and one PNG.

    The TAIL signal set is assumed (each excitation is followed by 28 s of
    silence folded into its own segment), which is what capture/grids/rev.json
    prescribes.
    """
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    y, fs, info, layout = load_rev(path)
    outdir = outdir or os.path.dirname(os.path.abspath(path))
    name = os.path.splitext(os.path.basename(path))[0]
    res = {"align": info, "segments": {}}
    fig, axs = plt.subplots(1, 2, figsize=(13, 4.5))
    names = [s["name"] for s in layout["segments"] if s["name"] != "silence"]
    for s in names:
        i0, cut_ = tail(y, fs, layout, s, pre_s=0.02, dur_s=None)
        L = cut_[i0:, 0]
        if s == "sine20":
            L = L[int(4.05 * fs):]
        c = edc(L)
        axs[0].plot(np.arange(len(c)) / fs, c, lw=0.8, label=s)
        rt, slope, r2, span = decay_slope(L, fs, -5.0, -45.0, t_skip=0.02)
        if not np.isfinite(r2) or r2 < 0.95:
            # not enough dynamic range above the noise floor for a T40 --
            # the click tail is usually only good for 25 dB
            rt, slope, r2, span = decay_slope(L, fs, -5.0, -25.0, t_skip=0.02)
        res["segments"][s] = {"rt60_s": rt, "slope_db_per_s": slope, "r2": r2, "fit_span_s": span,
                              "peak_dbfs": float(20 * np.log10(np.max(np.abs(L)) + 1e-12))}
    axs[0].set_xlim(0, 14); axs[0].set_ylim(-80, 2); axs[0].grid(alpha=0.3)
    axs[0].set_xlabel("s"); axs[0].set_ylabel("Schroeder EDC, dB"); axs[0].legend(fontsize=8)
    axs[0].set_title(name.replace("AX30G_REV_", ""), fontsize=8)

    i0, cut_ = tail(y, fs, layout, "clicks", pre_s=0.02, dur_s=None)
    bands = (125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 12000.0)
    bb = octave_bands(cut_[i0:, 0], fs, bands)
    fcs, rts = [], []
    for fc, v in bb.items():
        rt, _, r2, span = decay_slope(v, fs, -5.0, -25.0, t_skip=0.02)
        # the click tail only clears the noise floor by ~25 dB; a band whose
        # fit does not hold is reported as nan rather than as a wrong number
        if not np.isfinite(r2) or r2 < 0.90 or span < 0.05:
            rt = float("nan")
        fcs.append(fc); rts.append(rt)
    res["click_band_rt60"] = {int(a): float(b) for a, b in zip(fcs, rts)}
    axs[1].semilogx(fcs, rts, "o-")
    axs[1].set_xlabel("Hz"); axs[1].set_ylabel("T20-derived RT60, s"); axs[1].grid(alpha=0.3, which="both")
    axs[1].set_title("click tail, per octave band", fontsize=9)
    plt.tight_layout()
    png = os.path.join(outdir, name + ".png")
    plt.savefig(png, dpi=110)
    plt.close(fig)
    res["png"] = png
    return res
