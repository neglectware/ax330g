"""Sensitivity = the Schmitt trigger's hysteresis. With every other sweep parameter frozen at the
fit_sweep.py result, a 1-D grid (0.25 dB steps, 0.5..30 dB -- the whole budget, fixed in advance)
per Sensitivity row, scored on DI notes 1-4 (47.7-51.4 s); notes 5-7 (51.4-55.7 s) and the
pre-DI segments are held out. Reports trigger counts too."""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fit_sweep import *

d = json.load(open(os.path.join(H2, "sweep_fit.json")))["params"]
grid = np.arange(0.5, 30.01, 0.25)


def score(n, h, mask):
    quiet()
    mf = row_model(n, d, hyst=h)
    k = mask & ~np.isnan(mf) & ~np.isnan(MEAS[n])
    e = mf[k] - MEAS[n][k]
    c = 0.05
    return float(np.mean(2 * c * c * (np.sqrt(1 + (e / c) ** 2) - 1))), float(np.median(np.abs(1200 * np.log2(fc_of(mf[k]) / fc_of(MEAS[n][k])))))


out = {}
rows = sorted(sens_rows + [n for n in fit_rows if ROWS[n]["Depth"] == 50 and ROWS[n]["Decay"] == 25 and ROWS[n]["Pol"] == "UP"],
              key=lambda n: ROWS[n]["Sensitivity"])
for n in rows:
    S = ROWS[n]["Sensitivity"]
    sc = [score(n, h, m_di14)[0] for h in grid]
    i = int(np.argmin(sc))
    # the flat range: every h within 2 % of the best
    ok = grid[np.array(sc) <= sc[i] * 1.02]
    h = float(grid[i])
    fit = score(n, h, m_di14)
    held = score(n, h, m_di57)
    pre = score(n, h, m_fit)
    E = detector(xd, d["tau_det_ms"])
    fires = triggers(E, d["T_on_db"], h)
    di_f = [round(f / FSD, 2) for f in fires if 47.7 * FSD <= f < 55.7 * FSD]
    out[S] = {"hyst_db": h, "range": [float(ok.min()), float(ok.max())], "fit_notes1_4": fit, "held_notes5_7": held, "pre_DI": pre, "DI_triggers": di_f}
    print("S%-2d hyst %.2f dB (flat %.2f-%.2f)  fit(n1-4) cost %.5f median %.0f c | held(n5-7) %.5f %.0f c | pre-DI %.5f %.0f c | DI triggers %s"
          % (S, h, ok.min(), ok.max(), fit[0], fit[1], held[0], held[1], pre[0], pre[1], di_f), flush=True)
json.dump(out, open(os.path.join(H2, "sens_fit.json"), "w"), indent=1)
