"""Q(Resonance) from the noise-TF ratios R_x/R0 on the T2 H50 rows (and the old T1 H0 R25/R50 rows),
modelled as |LP(389, Q_x)|/|LP(389, Q_0)| smoothed exactly like the measurement (1/24 octave on the
Welch 8192 grid). Q_0 is shared. Ring-decay Q from the T1 H0 clicks is the independent check."""
import numpy as np, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from q import *
from static_law import lp2_mag
from scipy.optimize import minimize

G = np.geomspace(40, 16000, 240)
fw = np.fft.rfftfreq(8192, 1 / 48000.0)


def sm(v):
    out = np.empty(len(G))
    for i, g in enumerate(G):
        m = (fw >= g * 2 ** (-1 / 48)) & (fw <= g * 2 ** (1 / 48))
        out[i] = np.mean(v[m]) if m.any() else np.nan
    return out


def model_ratio(Qx, Q0, f0=389.0):
    a = 20 * np.log10(lp2_mag(np.maximum(fw, 1e-3), f0, Qx)) - 20 * np.log10(lp2_mag(np.maximum(fw, 1e-3), f0, Q0))
    return sm(a)


sets = {}
r0 = [n for n in find(Type=2, Harmonics=50, Depth=0, Resonance=0, DirectLevel=0, EffectLevel=50) if P(n)['in'] == 'LIN' and P(n)['take2']][0]
for R in (10, 25, 37, 50):
    ns = [n for n in find(Type=2, Harmonics=50, Depth=0, Resonance=R, DirectLevel=0, EffectLevel=50)]
    sets['T2H50 R%d' % R] = (R, [np.array(F[n]['noise']['H_db']) - np.array(F[r0]['noise']['H_db']) for n in ns])
t1r0 = find(Type=1, Harmonics=0, Depth=0, Resonance=0, DirectLevel=0, EffectLevel=50)[0]
for R in (25, 50):
    ns = find(Type=1, Harmonics=0, Depth=0, Resonance=R, DirectLevel=0, EffectLevel=50)
    sets['T1H0 R%d (old)' % R] = (R, [np.array(F[n]['noise']['H_db']) - np.array(F[t1r0]['noise']['H_db']) for n in ns])

band = (G > 200) & (G < 800)


def cost(p, which):
    Q0 = np.exp(p[0]); e = []
    for k, (R, curves) in which.items():
        Qx = np.exp(p[1 + list(which).index(k)])
        mr = model_ratio(Qx, Q0)
        for c in curves:
            e.append((mr[band] - c[band]))
    e = np.concatenate(e)
    return float(np.sqrt(np.nanmean(e ** 2)))


for label, which in (("T2H50 (new)", {k: v for k, v in sets.items() if k.startswith('T2')}),
                     ("T1H0 (old)", {k: v for k, v in sets.items() if k.startswith('T1')})):
    best = None
    for q0 in (2.5, 3.3, 4.28, 6.0):
        p0 = [np.log(q0)] + [np.log(q0 * 2)] * len(which)
        r = minimize(cost, p0, args=(which,), method="Nelder-Mead", options={"maxiter": 4000, "xatol": 1e-4, "fatol": 1e-5})
        if best is None or r.fun < best.fun:
            best = r
    print(label, "rms %.2f dB over %d curves, 200-800 Hz" % (best.fun, sum(len(v[1]) for v in which.values())))
    print("   Q0 = %.2f" % np.exp(best.x[0]), "  ".join("%s: Q %.2f" % (k, np.exp(best.x[1 + i])) for i, k in enumerate(which)))
