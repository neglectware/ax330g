"""REV pass-2 analysis: the hot-broadband (`SET-rev`) captures.

`analysis/rev.py` is hardwired to the TAIL signal set and to FS_DEV 39062.5.
This module is the pass-2 tooling: the REV signal set (`capture/layout-rev.json`,
a -6 dBFS click / -6 dBFS 5 s log sweep / -6 dBFS 60 ms burst, each with a 28 s
tail), the adopted device clock 39063.829787 Hz (= 153/188 of 48 kHz exactly,
`docs/sdly-clock-2026-09-16.md`), and Farina deconvolution of the sweep.

Alignment is on the reference channel (channel 2) against the sweep, which is
what `analysis/align.py` does; `align_capture` itself cannot be used because it
reads a `sine20` segment that the REV set does not have.
"""
import os
import sys
import json
from fractions import Fraction
import numpy as np
from scipy.signal import resample_poly, correlate, butter, sosfilt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)

from analysis.util import load_wav, load_layout, seg, cut          # noqa: E402
from engine.render import chain_fir                                 # noqa: E402

FS_DEV = 39063.829787           # 153/188 of 48000, the adopted device clock
DEV_UP, DEV_DOWN = 153, 188
FS_WORK = 48000.0
CAP_DIR = os.path.join(ROOT, "captures")
SIGNAL = os.path.join(ROOT, "capture", "signalset-rev.wav")
LAYOUT = os.path.join(ROOT, "capture", "layout-rev.json")
SETS = {"rev": (SIGNAL, LAYOUT),
        "tail": (os.path.join(ROOT, "capture", "signalset-tail.wav"),
                 os.path.join(ROOT, "capture", "layout-tail.json"))}
CHAIN = os.path.join(ROOT, "models", "ax30g-chain.json")

_cache = {}


def set_of(path):
    """Which signal set a capture name belongs to."""
    return "tail" if "SET-tail" in os.path.basename(path) else "rev"


def signal(which="rev"):
    x, fs = load_wav(SETS[which][0])
    return x[:, 0], fs


def layout(which="rev"):
    return load_layout(SETS[which][1])


def _peak_offset(template, sig):
    c = correlate(sig, template, mode="full", method="fft")
    k = int(np.argmax(np.abs(c)))
    if 0 < k < len(c) - 1:
        a, b, d = abs(c[k - 1]), abs(c[k]), abs(c[k + 1])
        den = a - 2 * b + d
        frac = 0.5 * (a - d) / den if den else 0.0
    else:
        frac = 0.0
    return k - (len(template) - 1) + frac


def load(path, fs_work=FS_WORK):
    """Aligned (N,3) capture at fs_work (L, R, reference), plus an info dict."""
    key = (path, fs_work)
    if key in _cache:
        return _cache[key]
    if not os.path.isabs(path) and not os.path.exists(path):
        path = os.path.join(CAP_DIR, path)
    which = set_of(path)
    x, fs_sig = signal(which)
    lay = layout(which)
    cap, fs = load_wav(path)
    if fs != fs_sig:
        fr = Fraction(int(fs_sig), int(fs))
        cap = resample_poly(cap, fr.numerator, fr.denominator, axis=0)
        fs = fs_sig
    ref = cap[:, 2]
    names = [q["name"] for q in lay["segments"]]
    # each excitation is followed by a long silent tail folded into its own
    # segment; correlate on the excitation itself only
    if "sweep" in names:
        s = seg(lay, "sweep"); dur = s["sweep_s"]
    elif "sine20" in names:
        s = seg(lay, "sine20"); dur = s["tone_s"]
    else:
        s = seg(lay, "burst_hot"); dur = s["len_s"]
    tpl = cut(x, fs, s["start"], s["start"] + dur + 0.1)
    lag = _peak_offset(tpl, ref)
    offset = lag / fs - s["start"]
    info = {"offset_s": float(offset),
            "ref_peak_dbfs": float(20 * np.log10(np.max(np.abs(ref)) + 1e-12))}
    r = cut(ref, fs, offset + s["start"], offset + s["start"] + dur)
    xx = cut(x, fs, s["start"], s["start"] + dur)
    info["ref_gain_db"] = float(20 * np.log10(
        (np.sqrt(np.mean(r ** 2)) + 1e-12) / (np.sqrt(np.mean(xx ** 2)) + 1e-12)))
    n0 = int(round(offset * fs))
    length = len(x) + int(3 * fs)
    out = np.zeros((length, 3))
    src = cap[max(n0, 0): max(n0, 0) + length, :3]
    d0 = max(-n0, 0)
    out[d0:d0 + len(src)] = src
    info["subsample_residual_ms"] = float((offset * fs - n0) / fs * 1000)
    if fs_work != fs:
        fr = Fraction(fs_work / fs).limit_denominator(4096)
        out = resample_poly(out, fr.numerator, fr.denominator, axis=0)
    _cache[key] = (out, float(fs_work), info, lay)
    return _cache[key]


_chain = None


def chain_spec():
    global _chain
    if _chain is None:
        with open(CHAIN) as f:
            _chain = json.load(f)
    return _chain


def deconv_chain(y, fs, reg_db=-60.0, hp_hz=18.0):
    """Remove the measured converter chain's HF FIR (regularised spectral
    division). The three 5.49 Hz high-passes are left in (inverting them is a
    triple integrator), exactly as analysis/rev.py does, so the IR keeps the
    unit's own LF roll-off; a 2nd-order 18 Hz high-pass keeps DC drift out."""
    ch = chain_spec()
    out = np.asarray(y, dtype=float)
    single = out.ndim == 1
    if single:
        out = out[:, None]
    h, pre = chain_fir(ch, fs)
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
    if hp_hz:
        out = sosfilt(butter(2, hp_hz / (fs / 2), btype="highpass", output="sos"), out, axis=0)
    return out[:, 0] if single else out


def to_device(y):
    return resample_poly(y, DEV_UP, DEV_DOWN, axis=0)


def ir(path, segment="sweep", dur_s=12.0, pre_s=0.02, reg_db=-70.0,
       bl_hz=(15000.0, 18800.0), channels=(0, 1), hp_hz=18.0):
    """Device-rate impulse response from one segment of one capture.

    Returns (i0, h) with h (N, len(channels)) at FS_DEV and the excitation's
    t = 0 at index i0. The excitation is divided out on the device grid with
    the SAME regularisation for every capture, so amplitudes are comparable
    across captures.
    """
    y, fs, info, lay = load(path)
    x, fs_sig = signal(set_of(path))
    s = seg(lay, segment)
    t0 = s.get("click_times", s.get("burst_times", [s["start"]]))[0]
    a = int(round((t0 - pre_s) * fs))
    b = int(round((t0 + dur_s) * fs))
    capseg = y[a:b, list(channels)]
    exc = x[a:b]
    z = deconv_chain(capseg, fs, hp_hz=hp_hz)
    r = to_device(z)
    e = to_device(exc)
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
    i0 = int(round(pre_s * FS_DEV))
    cols = [np.roll(np.fft.irfft(np.fft.rfft(r[:, c], n) * Einv, n), i0)[: len(r)]
            for c in range(r.shape[1])]
    return i0, np.stack(cols, axis=1), info


def name(Type, RevTime, Balance=50, HighDamp=0, PreDly=1, take=None, which="rev"):
    rt = ("%g" % RevTime)
    s = (f"AX30G_REV_HighDamp-{HighDamp:g}_Balance-{Balance:g}_Type-{Type}"
         f"_PreDly-{PreDly:g}_RevTime-{rt}_IN-LIN_SET-{which}")
    if take:
        s += f"_take{take}"
    return s + ".wav"


# ---------------------------------------------------------------- tap reading

def tap_template(h, i0, first, half=6, n=24):
    """The shape one arrival takes in the device-rate IR (resampling kernel +
    whatever the chain deconvolution leaves), read off an isolated arrival.
    Returns (t, k0) with t normalised to unit peak and k0 the peak index in t."""
    a = i0 + first - half
    t = h[a:a + n].copy()
    k0 = int(np.argmax(np.abs(t)))
    t = t / t[k0]
    return t, k0


def deconv_taps(x, tmpl, k0, reg_db=-42.0):
    """Divide the tap template out of x so each arrival becomes one sample at
    its own integer device position."""
    n = 1
    while n < len(x) + len(tmpl):
        n *= 2
    T = np.fft.rfft(tmpl, n)
    mag = np.abs(T)
    eps = (10.0 ** (reg_db / 20.0)) * mag.max()
    Tinv = np.conj(T) / (mag ** 2 + eps ** 2)
    y = np.fft.irfft(np.fft.rfft(x, n) * Tinv, n)
    return np.roll(y, k0)[: len(x)]


def taplist(y, i0, thr, lo=0, hi=None, merge=2):
    """Local maxima of |y| above thr, as (position relative to i0, amplitude)."""
    hi = hi or len(y)
    out = []
    a = np.abs(y)
    k = i0 + lo
    while k < min(i0 + hi, len(y) - 1):
        if a[k] > thr and a[k] >= a[k - 1] and a[k] >= a[k + 1]:
            out.append((k - i0, float(y[k])))
            k += merge
        else:
            k += 1
    return out


# ------------------------------------------------------------------ nulling

def null_fast(a, b, span=1.5, iters=36):
    """Same contract as analysis.rev.null_pair (align b onto a with one
    fractional lag and one gain, return the residual in dB relative to a) but
    the FFT of b is computed once instead of once per golden-section step,
    which matters on the 28 s reverb tails."""
    from scipy.signal import correlate
    a = np.asarray(a, float)
    b = np.asarray(b, float)
    c = correlate(a, b, mode="full", method="fft")
    k = int(np.argmax(np.abs(c)))
    if 0 < k < len(c) - 1:
        y0, y1, y2 = abs(c[k - 1]), abs(c[k]), abs(c[k + 1])
        den = y0 - 2 * y1 + y2
        frac = 0.5 * (y0 - y2) / den if den else 0.0
    else:
        frac = 0.0
    lag0 = (k - (len(b) - 1)) + frac
    n = 1
    while n < len(b) * 2:
        n *= 2
    B = np.fft.rfft(b, n)
    f = np.fft.rfftfreq(n)
    aa = float(np.dot(a, a))

    def cost(lag):
        m = np.fft.irfft(B * np.exp(-2j * np.pi * f * lag), n)[: len(b)]
        mm = float(np.dot(m, m))
        g = float(np.dot(m, a)) / max(mm, 1e-30)
        return aa - 2 * g * float(np.dot(m, a)) + g * g * mm, g, m

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
    _, g, m = cost(lag)
    r = a - g * m
    null = 20 * np.log10(np.sqrt(np.mean(r ** 2)) / max(np.sqrt(np.mean(a ** 2)), 1e-30))
    return null, lag, g, r
