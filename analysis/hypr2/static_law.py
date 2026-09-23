"""Static sweep law from the 1 kHz ramp: for every ramp frame, the ratio of the row's harmonic
amplitudes H1..H8 to the Depth-0 row's is R(k kHz; fc, Q)/R(k kHz; 389, Q). Solve fc per frame by a
grid search. Prints fc vs input level per row. Also the steady sine20 (last 2 s)."""
import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from q import *
from meas import load, cut, FS, db, LAY, seg
import numpy as np
from scipy.signal import stft

FSD = 39062.5
K = np.arange(1, 9)


def lp2_mag(f, f0, Q, fs=FSD):
    # RBJ LP magnitude evaluated on the unit circle (digital, device rate)
    f0 = np.minimum(f0, 0.45 * fs)
    w0 = 2 * np.pi * f0 / fs
    al = np.sin(w0) / (2 * Q)
    c = np.cos(w0)
    b = np.array([(1 - c) / 2, 1 - c, (1 - c) / 2]) / (1 + al)
    a = np.array([1 + al, -2 * c, 1 - al]) / (1 + al)
    z = np.exp(-1j * 2 * np.pi * np.asarray(f) / fs)
    H = (b[0] + b[1] * z + b[2] * z ** 2) / (a[0] + a[1] * z + a[2] * z ** 2)
    return np.abs(H)


def harm_frames(y, t0, t1, nper=2048, hop=512):
    c = cut(y, FS, t0, t1)
    f, t, Z = stft(c, FS, nperseg=nper, noverlap=nper - hop, boundary=None, padded=False)
    A = np.abs(Z)
    out = []
    for k in K:
        i = int(round(1000 * k * nper / FS))
        out.append(np.max(A[i - 2:i + 3, :], axis=0))
    return t + t0, 20 * np.log10(np.array(out) + 1e-15)  # (8, frames)


FGRID = np.geomspace(60, 17000, 400)


def solve_fc(ratio_db, live, Q, f_rest=389.0, pol_sign=None):
    """least squares over live harmonics; ratio includes an unknown broadband gain? No: resonator
    gain at DC is unity, so no free gain. Returns (fc, rms)."""
    base = 20 * np.log10(lp2_mag(1000 * K, f_rest, Q))
    best = (np.nan, np.inf)
    errs = []
    for fc in FGRID:
        pred = 20 * np.log10(lp2_mag(1000 * K, fc, Q)) - base
        e = np.sqrt(np.mean((pred[live] - ratio_db[live]) ** 2)) if live.any() else np.inf
        errs.append(e)
    errs = np.array(errs)
    i = int(np.argmin(errs))
    return FGRID[i], errs[i]


def row_static(n, ref, Q, seg_name="ramp", t_extra=0.0):
    s = seg(LAY, seg_name)
    t, Hn = harm_frames(load(n), s["start"], s["end"] + t_extra)
    _, Hr = harm_frames(load(ref), s["start"], s["end"] + t_extra)
    res = []
    for j in range(Hn.shape[1]):
        live = (Hr[:, j] > -105) & (Hn[:, j] > -110)
        fc, e = solve_fc(Hn[:, j] - Hr[:, j], live, Q)
        res.append((t[j], fc, e, int(live.sum())))
    return res


if __name__ == "__main__":
    Q = float(sys.argv[1]) if len(sys.argv) > 1 else 8.2
    ref = find(Type=2, Harmonics=50, Resonance=25, Depth=0, DirectLevel=0, take2=False)[0]
    rows = [n for n in find(Type=2, Harmonics=50, Resonance=25, DirectLevel=0) if P(n)["Depth"] > 0] + \
           find(Type=2, Harmonics=50, Resonance=25, Depth=0, DirectLevel=0, take2=True)
    lv = [-38, -35, -32, -30, -28, -26, -24, -22, -20, -16, -12, -8, -4, -1]
    print("in dBFS (ramp)      ", " ".join("%6d" % v for v in lv))
    out = {}
    for n in rows:
        r = row_static(n, ref, Q)
        t = np.array([a[0] for a in r]); fc = np.array([a[1] for a in r]); e = np.array([a[2] for a in r])
        L = -40 + 40 * (t + 2048 / 2 / FS - 30.2) / 5.0
        idx = [int(np.argmin(abs(L - v))) for v in lv]
        out[n] = {"L": L.tolist(), "fc": fc.tolist(), "err": e.tolist()}
        print("%-44s" % short(n)[:44], " ".join("%6.0f" % fc[i] for i in idx), " err med %.1f" % np.median(e))
    json.dump(out, open(os.path.join(H2, "static_ramp.json"), "w"))
