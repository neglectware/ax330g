"""Driver re-fit, separable form (2026-09-23). A hard clip makes only odd harmonics and a full-wave
rectifier only even ones, so the two branches are fitted separately:
  even branch per Harmonics (shared by both Types): gain g_e on a 0.5 dB grid from -20 to +90 dB,
     weight in closed form (log-domain least squares); targets H2, H4 of sine20 and H2 of sine40 on
     BOTH Types (6 levels).
  odd branch per (Type, Harmonics): same grid; targets H1, H3, H5 of sine20 and H1, H3 of sine40.
  One shared output gain is not separable from the weights, so it is fixed at 0 dB here and the
  weights carry it (the model file folds it back into resonator.gain_db).
Levels below -108 dBFS in the capture are floor ('at or below').
The whole budget is the grid (221 gains per branch), fixed in advance.
Held out: the ramp describing function (tested by eval_driver.py)."""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fit_driver as FD
from q import *
H2d = os.path.dirname(os.path.abspath(__file__))
GR = np.arange(-20, 90.01, 0.5)
FLOOR = -108.0

def odd_h(g, xs, skip, nh):
    y = FD.post(np.clip(10**(g/20)*xs, -1, 1)); return FD.harm(y, skip, nh)
def even_h(g, xs, skip, nh):
    from scipy.signal import lfilter
    r = np.clip(np.abs(10**(g/20)*xs), 0, 1); r = lfilter([1,-1],[1,-FD.HP_A], r)
    return FD.harm(FD.post(r), skip, nh)

def best_w(m, t):
    live = t > FLOOR
    if not live.any(): return -120.0, 0.0
    w = float(np.mean(t[live] - m[live]))
    e = m + w - t
    e = np.where(live, e, np.maximum(m + w - FLOOR, 0))
    return w, float(np.sqrt(np.mean(e**2)))

out = {"even": {}, "odd": {}}
for H in FD.HS:
    tg = []
    for T in (1, 2):
        t20, t40 = FD.TARGET[(T, H)]
        tg.append((t20[[1, 3]], t40[[1]]))
    best = None
    for g in GR:
        m20 = even_h(g, FD.S20, 0.6, 5)[[1, 3]]; m40 = even_h(g, FD.S40, 0.3, 3)[[1]]
        m = np.concatenate([m20, m40, m20, m40]); t = np.concatenate([tg[0][0], tg[0][1], tg[1][0], tg[1][1]])
        w, e = best_w(m, t)
        if best is None or e < best[2]: best = (float(g), w, e, (m + w).round(1).tolist(), t.round(1).tolist())
    out["even"][H] = best
    print("even H%-2d g %.1f dB w %.1f dB rms %.2f | model %s capture %s" % ((H,) + best[:3] + (best[3], best[4])), flush=True)
for T in (1, 2):
    for H in FD.HS:
        t20, t40 = FD.TARGET[(T, H)]
        t = np.concatenate([t20[[0, 2, 4]], t40[[0, 2]]])
        best = None
        for g in GR:
            m = np.concatenate([odd_h(g, FD.S20, 0.6, 5)[[0, 2, 4]], odd_h(g, FD.S40, 0.3, 3)[[0, 2]]])
            w, e = best_w(m, t)
            if best is None or e < best[2]: best = (float(g), w, e, (m + w).round(1).tolist(), t.round(1).tolist())
        out["odd"]["%d,%d" % (T, H)] = best
        print("odd T%d H%-2d g %.1f dB w %.1f dB rms %.2f | model %s capture %s" % ((T, H) + best[:3] + (best[3], best[4])), flush=True)
json.dump(out, open(os.path.join(H2d, "driver_fit2.json"), "w"), indent=1)
