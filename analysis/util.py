import json
import numpy as np
import soundfile as sf
from scipy.signal import get_window


def load_wav(path):
    x, fs = sf.read(path, dtype="float64", always_2d=True)
    return x, fs


def load_layout(path):
    with open(path) as f:
        return json.load(f)


def seg(layout, name):
    for s in layout["segments"]:
        if s["name"] == name:
            return s
    raise KeyError(name)


def cut(x, fs, t0, t1):
    return x[int(round(t0 * fs)):int(round(t1 * fs))]


def band_spectrum(x, fs, f_lo=50.0, f_hi=19000.0, n_bands=60, win="hann"):
    """Magnitude spectrum averaged into log-spaced bands. Returns (f_centers, mag)."""
    n = len(x)
    w = get_window(win, n)
    X = np.abs(np.fft.rfft(x * w))
    f = np.fft.rfftfreq(n, 1.0 / fs)
    edges = np.geomspace(f_lo, f_hi, n_bands + 1)
    mags = np.full(n_bands, np.nan)
    for i in range(n_bands):
        m = (f >= edges[i]) & (f < edges[i + 1])
        if m.any():
            mags[i] = np.sqrt(np.mean(X[m] ** 2))
    fc = np.sqrt(edges[:-1] * edges[1:])
    return fc, mags


def welch_bands(x, fs, nfft=4096, **kw):
    """Welch-style band spectrum (average of band_spectrum over frames)."""
    hop = nfft // 2
    acc = None
    cnt = 0
    for s in range(0, len(x) - nfft + 1, hop):
        fc, m = band_spectrum(x[s:s + nfft], fs, **kw)
        acc = m ** 2 if acc is None else acc + m ** 2
        cnt += 1
    return fc, np.sqrt(acc / max(cnt, 1))


def db(v):
    return 20.0 * np.log10(np.maximum(np.asarray(v, dtype=float), 1e-12))
