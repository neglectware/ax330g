"""Per-row corner-trajectory error, old envelope model vs new trigger-EG model, on the fit segments
(t < 47.5 s) and the held-out DI clip. Median |cents| over valid frames with either track above 450 Hz,
and the share of frames within 300 cents."""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fit_sweep import *
from pyguard import quiet
ROOT = "/path/to/ax30g"
sys.path.insert(0, ROOT)
import engine.hypr as EH

new = json.load(open(os.path.join(H2, "sweep_fit.json")))["params"]
sens = json.load(open(os.path.join(H2, "sens_fit.json")))
old_spec = json.load(open(os.path.join(ROOT, "models", "ax30g-hypr.json")))


def old_track(n):
    p = ROWS[n]
    spec = json.loads(json.dumps(old_spec))
    spec["params"].update({"Type": 2, "Harmonics": 50, "Sensitivity": p["Sensitivity"], "Depth": p["Depth"],
                           "Decay": p["Decay"], "Polarity": p["Pol"], "Resonance": p["Resonance"]})
    truth = {}
    f = EH._envelope_sweep(xd, None, spec, FSD, truth)
    return smooth_at_frames(F_of(f))


def stats(mf, me, mask):
    k = mask & ~np.isnan(mf) & ~np.isnan(me) & ((me > FLO + 1e-9) | (mf > FLO + 1e-9))
    c = np.abs(1200 * np.log2(fc_of(mf[k]) / fc_of(me[k])))
    return float(np.median(c)), float(np.mean(c < 300) * 100), int(k.sum())


rows = sorted([n for n, p in ROWS.items() if p["Depth"] > 0], key=lambda n: (ROWS[n]["Pol"], ROWS[n]["Sensitivity"], ROWS[n]["Decay"], ROWS[n]["Depth"]))
res = {}
print("%-44s | %-26s | %-26s" % ("row", "fit segs: old / new (med c, %<300c)", "held-out DI: old / new"))
for n in rows:
    quiet()
    p = ROWS[n]
    h = sens[str(p["Sensitivity"])]["hyst_db"]
    mn = row_model(n, new, hyst=h)
    mo = old_track(n)
    me = MEAS[n]
    a, b = stats(mo, me, m_fit), stats(mn, me, m_fit)
    c, d = stats(mo, me, m_di), stats(mn, me, m_di)
    res[n] = {"fit_old": a, "fit_new": b, "di_old": c, "di_new": d}
    print("%-44s | %5.0f %3.0f%% / %5.0f %3.0f%% | %5.0f %3.0f%% / %5.0f %3.0f%%" % (short(n)[:44], a[0], a[1], b[0], b[1], c[0], c[1], d[0], d[1]), flush=True)
json.dump(res, open(os.path.join(H2, "eval_sweep.json"), "w"), indent=1)
