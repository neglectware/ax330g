"""Whole-file resonator-corner track: per STFT frame, the row's band spectrum divided by the Depth-0
reference row's is R(f; fc, Q)/R(f; f_rest, Q). Solve fc per frame (and optionally a DOWN rest)."""
import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from q import *
from meas import load, cut, FS, db, LAY, seg
from static_law import lp2_mag
import numpy as np
from scipy.signal import stft

NPER, HOP = 1024, 256
EDGES = np.geomspace(150, 15000, 37)          # 1/6-octave-ish bands (36 bands)
CEN = np.sqrt(EDGES[:-1] * EDGES[1:])
FG = np.geomspace(100, 19000, 300)


def band_spec(y):
    f, t, Z = stft(y, FS, nperseg=NPER, noverlap=NPER - HOP, boundary=None, padded=False)
    P = np.abs(Z) ** 2
    B = np.empty((len(CEN), P.shape[1]))
    for i in range(len(CEN)):
        m = (f >= EDGES[i]) & (f < EDGES[i + 1])
        B[i] = P[m].mean(axis=0)
    return t + NPER / 2 / FS, 10 * np.log10(B + 1e-30)


_TBL = {}


def table(Q, f_rest):
    k = (round(Q, 4), round(f_rest, 3))
    if k not in _TBL:
        base = 20 * np.log10(lp2_mag(CEN, f_rest, Q))
        _TBL[k] = np.array([20 * np.log10(lp2_mag(CEN, fc, Q)) - base for fc in FG])  # (300, 36)
    return _TBL[k]


def track(Bn, Br, Q=8.2, f_rest=389.0, floor_db=None, margin=12.0):
    T = table(Q, f_rest)
    if floor_db is None:
        floor_db = np.percentile(Br, 5, axis=1)[:, None]
    live = (Br > floor_db + margin)
    R = Bn - Br
    fc = np.full(R.shape[1], np.nan)
    err = np.full(R.shape[1], np.nan)
    for j in range(R.shape[1]):
        m = live[:, j]
        if m.sum() < 4:
            continue
        E = np.sqrt(np.mean((T[:, m] - R[m, j][None, :]) ** 2, axis=1))
        i = int(np.argmin(E))
        fc[j] = FG[i]
        err[j] = E[i]
    return fc, err, live.sum(axis=0)


def ref_row():
    return find(Type=2, Harmonics=50, Resonance=25, Depth=0, DirectLevel=0, take2=False)[0]


if __name__ == "__main__":
    ref = ref_row()
    t, Br = band_spec(load(ref))
    out = {"t": t.tolist()}
    names = [n for n in find(Type=2, Harmonics=50, Resonance=25, DirectLevel=0)]
    for n in names:
        _, Bn = band_spec(load(n))
        fc, err, nl = track(Bn, Br)
        out[n] = {"fc": fc.tolist(), "err": err.tolist(), "n": nl.tolist()}
        print("ok", short(n), "median err %.2f" % np.nanmedian(err), flush=True)
    json.dump(out, open(os.path.join(H2, "fctrack.json"), "w"))
