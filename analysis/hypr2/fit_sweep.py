"""Fit the trigger-EG sweep model to the measured corner tracks (fctrack.json).

Fit set  : T2 H50 R25 rows, UP Depth 10/25/37/50 (Dec25 S25), UP D50 Decay 0/10/37/50 (S25),
           DOWN Depth 10/25/50 (Dec25 S25) -- on everything BEFORE the DI clip (t < 47.5 s).
Held out : the DI clip (47.7-55.7 s) of every one of those rows, and the Sensitivity rows'
           notes 5-7 (51.4-55.7 s); the mixed hold-out row (grid row 35) entirely.
Criterion: soft-L1 (scale 0.05 in F) of (model F - measured F) over valid frames, F = 2 sin(pi fc/fs), both tracks
           clipped to fc in [450, 14000] Hz (the estimator's reliable range), model smoothed with
           the estimator's 1024-sample Hann^2 window. Nelder-Mead, budget FIXED IN ADVANCE:
           2 restarts x 1500 function evaluations, stop at whichever comes first.
"""
import sys, os, json, time
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from q import *
from ctrl import *
from scipy.optimize import minimize

H2 = os.path.dirname(os.path.abspath(__file__))
xd = np.load(os.path.join(H2, "xd_LIN.npy"))
T = json.load(open(os.path.join(H2, "fctrack.json")))
tf = np.array(T["t"])
FLO, FHI = F_of(450.0), F_of(14000.0)
W = np.hanning(int(round(1024 * FSD / 48000))) ** 2
W /= W.sum()
HW = len(W) // 2
idx_frames = np.round(tf * FSD).astype(int)


def smooth_at_frames(F):
    Fc = np.clip(F, FLO, FHI)
    out = np.empty(len(idx_frames))
    n = len(Fc)
    for j, c in enumerate(idx_frames):
        a, b = c - HW, c - HW + len(W)
        if a < 0 or b > n:
            out[j] = np.nan
            continue
        out[j] = np.dot(Fc[a:b], W)
    return out


def meas_F(n):
    fc = np.array(T[n]["fc"], dtype=float)
    return np.clip(F_of(np.clip(fc, 450, 14000)), FLO, FHI)


ROWS = {}
for n in T:
    if not n.startswith("AX30G"):
        continue
    p = P(n)
    ROWS[n] = p

fit_rows = [n for n, p in ROWS.items() if p["Sensitivity"] == 25 and (p["Depth"] > 0)]
sens_rows = [n for n, p in ROWS.items() if p["Sensitivity"] != 25 and p["Depth"] > 0]
m_fit = tf < 47.5
m_di = (tf >= 47.7) & (tf < 55.7)
m_di14 = (tf >= 47.7) & (tf < 51.4)
m_di57 = (tf >= 51.4) & (tf < 55.7)
MEAS = {n: meas_F(n) for n in ROWS}

NAMES = ["T_on_db", "tau_det_ms", "hyst_db", "tau_att_ms", "t_sw_ms", "k_up", "k_dn", "F_top", "F_max",
         "dec0", "dec10", "dec25", "dec37", "dec50"]


def unpack(v):
    d = dict(zip(NAMES, v))
    d["F_rest"] = F_of(389.0)
    return d


def row_model(n, d, hyst=None):
    p = ROWS[n]
    tau = d["dec%d" % p["Decay"]]
    q = dict(d)
    F, E, fires, g = control(xd, q, p["Depth"], tau, pol=p["Pol"], hyst_db=hyst)
    return smooth_at_frames(F)


def rms(a, b, m):
    k = m & ~np.isnan(a) & ~np.isnan(b)
    return float(np.sqrt(np.mean((a[k] - b[k]) ** 2))), int(k.sum())


from pyguard import quiet


def cost(v, rows=None, mask=m_fit):
    quiet()
    d = unpack(v)
    if d["tau_det_ms"] <= 0.2 or d["tau_att_ms"] <= 0.05 or min(d["dec0"], d["dec10"], d["dec25"], d["dec37"], d["dec50"]) <= 0.5 or d["t_sw_ms"] < 0 or d["hyst_db"] < 0:
        return 10.0
    e = []
    for n in (rows or fit_rows):
        mf = row_model(n, d)
        k = mask & ~np.isnan(mf) & ~np.isnan(MEAS[n])
        e.append((mf[k] - MEAS[n][k]))
    e = np.concatenate(e)
    # robust: the estimator throws isolated wild frames (fc pinned at the band top on
    # near-silent frames); soft-L1 with a 0.05 F-unit scale, reported as rms-equivalent
    c = 0.05
    return float(np.mean(2 * c * c * (np.sqrt(1 + (e / c) ** 2) - 1)))


x0 = np.array([-22.3, 20.0, 8.0, 5.0, 15.0, 0.036, 0.032, F_of(11500.0), F_of(15000.0),
               22.0, 28.0, 57.0, 280.0, 5000.0])

if __name__ == "__main__":
    t0 = time.time()
    c0 = cost(x0)
    print("start cost %.4f (%.1f s/eval)" % (c0, time.time() - t0), flush=True)
    best = None
    x = x0.copy()
    for rs in range(2):
        r = minimize(cost, x, method="Nelder-Mead",
                     options={"maxfev": 1500, "xatol": 1e-4, "fatol": 1e-6, "adaptive": True})
        print("restart %d: cost %.4f after %d evals (%s)" % (rs, r.fun, r.nfev, r.message), flush=True)
        if best is None or r.fun < best.fun:
            best = r
        x = r.x
    d = unpack(best.x)
    json.dump({"params": d, "cost_fit": best.fun}, open(os.path.join(H2, "sweep_fit.json"), "w"), indent=1)
    print(json.dumps(d, indent=1))
