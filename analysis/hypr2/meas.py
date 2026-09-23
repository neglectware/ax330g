"""Measurement helpers shared by the stage-2 scripts. All operate on a mono float array at 48 kHz
aligned to the normal signal set."""
import os, sys, json
import numpy as np
from scipy.signal import stft, butter, sosfiltfilt, hilbert, csd, welch

ROOT = "/path/to/ax30g"
sys.path.insert(0, ROOT)
from analysis.util import load_wav, load_layout, seg, cut

H2 = os.path.dirname(os.path.abspath(__file__))
FS = 48000
X, _ = load_wav(os.path.join(ROOT, "capture", "signalset-normal.wav"))
X = X[:, 0]
LAY = load_layout(os.path.join(ROOT, "capture", "layout-normal.json"))
GRID = np.geomspace(40, 16000, 240)


def db(v):
    return 20 * np.log10(np.maximum(np.abs(v), 1e-15))


def load(name):
    a = np.load(os.path.join(H2, "cache", name + ".npy"))
    return a[:, 0].astype(float)


def harmonics(y, t0=23.0, t1=25.0, nh=8, f0=1000.0):
    c = cut(y, FS, t0, t1)
    n = len(c)
    w = np.hanning(n)
    Y = np.abs(np.fft.rfft(c * w)) / (np.sum(w) / 2)
    out = []
    for k in range(1, nh + 1):
        i = int(round(f0 * k * n / FS))
        out.append(float(db(np.max(Y[i - 3:i + 4]))))
    return out


def tone_track(y, t0, t1, ks=(1, 2, 3), nper=2048, hop=512, f0=1000.0):
    c = cut(y, FS, t0, t1)
    f, t, Z = stft(c, FS, nperseg=nper, noverlap=nper - hop, boundary=None, padded=False)
    A = np.abs(Z) * 2  # stft already scales by window sum
    res = {"t": (t + t0).tolist()}
    for k in ks:
        i = int(round(f0 * k * nper / FS))
        res["H%d" % k] = db(np.max(A[i - 2:i + 3, :], axis=0)).tolist()
    return res


def ring(y, t_click, fband=(150, 900), dur=0.5):
    c = cut(y, FS, t_click + 0.002, t_click + dur)
    n = len(c)
    X_ = np.abs(np.fft.rfft(c * np.hanning(n)))
    f = np.fft.rfftfreq(n, 1 / FS)
    m = (f > fband[0]) & (f < fband[1])
    fp = float(f[m][np.argmax(X_[m])])
    return fp


def ring_decay(y, t_click, fc=389.0, bw_oct=1 / 3.0, dur=0.6):
    """Envelope decay of the ring around fc after a click: dB/s slope fitted between -3 and -25 dB
    below the envelope peak. Returns (slope_db_per_s, tau_s, Q_equiv, peak_db)."""
    lo, hi = fc * 2 ** (-bw_oct), fc * 2 ** bw_oct
    sos = butter(2, [lo, hi], btype="band", fs=FS, output="sos")
    c = cut(y, FS, t_click - 0.05, t_click + dur)
    b = sosfiltfilt(sos, c)
    e = db(np.abs(hilbert(b)))
    i0 = int(0.05 * FS)
    ee = e[i0:]
    k = int(np.argmax(ee[: int(0.1 * FS)]))
    pk = ee[k]
    t = np.arange(len(ee)) / FS
    sel = (np.arange(len(ee)) > k) & (ee < pk - 3) & (ee > pk - 25)
    # take the first contiguous region only
    idx = np.where(sel)[0]
    if len(idx) < 50:
        return None
    brk = np.where(np.diff(idx) > 1)[0]
    if len(brk):
        idx = idx[: brk[0] + 1]
    if len(idx) < 50:
        return None
    p = np.polyfit(t[idx], ee[idx], 1)
    slope = p[0]
    tau = -20 / np.log(10) / slope if slope < 0 else float("inf")  # amplitude tau
    return {"slope_db_s": float(slope), "tau_s": float(tau), "Q": float(np.pi * fc * tau), "peak_db": float(pk)}


def smooth_log(f, v, frac=1 / 24.0):
    out = np.empty(len(GRID))
    for i, g in enumerate(GRID):
        m = (f >= g * 2 ** (-frac / 2)) & (f <= g * 2 ** (frac / 2))
        out[i] = np.mean(v[m]) if m.any() else np.nan
    return out


def noise_tf(y, x=None):
    s = seg(LAY, "noise")
    x = X if x is None else x
    xc = cut(x, FS, s["start"] + 0.1, s["end"] - 0.1)
    yc = cut(y, FS, s["start"] + 0.1, s["end"] - 0.1)
    f, Pxy = csd(xc, yc, FS, nperseg=8192)
    f, Pxx = welch(xc, FS, nperseg=8192)
    f, Pyy = welch(yc, FS, nperseg=8192)
    H = np.abs(Pxy) / Pxx
    return {"H_db": smooth_log(f, db(H)).tolist(), "Pyy_db": smooth_log(f, 10 * np.log10(Pyy + 1e-30)).tolist()}


def sweep_resp(y):
    """Output level at the instantaneous frequency of the exponential sine sweep (the fundamental),
    on GRID."""
    s = seg(LAY, "sweep")
    T = s["end"] - s["start"]
    f0, f1 = s["f0"], s["f1"]
    c = cut(y, FS, s["start"], s["end"])
    f, t, Z = stft(c, FS, nperseg=2048, noverlap=2048 - 256, boundary=None, padded=False)
    A = np.abs(Z) * 2
    finst = f0 * (f1 / f0) ** (t / T)
    vals = []
    for j, fi in enumerate(finst):
        i = int(round(fi * 2048 / FS))
        vals.append(float(db(np.max(A[max(i - 2, 0):i + 3, j]))))
    vals = np.array(vals)
    out = np.interp(np.log(GRID), np.log(finst), vals, left=np.nan, right=np.nan)
    return out.tolist()


def peak_track(y, t0, t1, fmin=120.0, fmax=15000.0, nper=2048, hop=256):
    c = cut(y, FS, t0, t1)
    f, t, Z = stft(c, FS, nperseg=nper, noverlap=nper - hop, boundary=None, padded=False)
    A = db(np.abs(Z))
    m = (f >= fmin) & (f <= fmax)
    idx = np.where(m)[0]
    k = idx[np.argmax(A[m], axis=0)]
    # parabolic refinement in dB
    a = A[k - 1, np.arange(A.shape[1])]
    b = A[k, np.arange(A.shape[1])]
    cc = A[k + 1, np.arange(A.shape[1])]
    den = a - 2 * b + cc
    d = np.where(den != 0, 0.5 * (a - cc) / den, 0.0)
    fr = (k + np.clip(d, -0.5, 0.5)) * FS / nper
    # centroid (power weighted, log-frequency) in 150..15k as a second estimator
    P = np.abs(Z[m]) ** 2
    cen = np.exp(np.sum(P * np.log(f[m])[:, None], axis=0) / np.maximum(np.sum(P, axis=0), 1e-30))
    return {"t": (t + t0 + nper / 2 / FS * 0).tolist(), "f": fr.tolist(), "a": b.tolist(), "cen": cen.tolist()}


def input_env(x, t0, t1, ta_ms=1.2, tr_ms=48.0):
    import math
    c = np.abs(cut(x, FS, t0, t1))
    aa = 1 - math.exp(-1 / (ta_ms * 1e-3 * FS))
    ar = 1 - math.exp(-1 / (tr_ms * 1e-3 * FS))
    from scipy.signal import lfilter
    # exact peak follower via loop (fast enough for <10 s)
    e = 0.0
    out = np.empty(len(c))
    for i in range(len(c)):
        v = c[i]
        e += (aa if v > e else ar) * (v - e)
        out[i] = e
    return out
