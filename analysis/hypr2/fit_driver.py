"""Driver re-fit, 2026-09-23.

Structure (new): y = w_odd(T,H) * clip(g(T,H) x)  +  w_e(H) * DCblock(clip(|g_e(H) x|))
  -- the even branch has its own gain and weight, SHARED by Type 1 and Type 2 (the captures give
     identical H2/H4 in both Types at every Harmonics setting). Then the static resonator
     (389 Hz, Q0) and the fixed 5706 Hz / Q 1.92 low-pass, and ONE shared output gain.
Old structure (for comparison): even branch uses the Type's own g and a per-(T,H) w_even.

Fit targets: steady sine20 (23.0-25.0 s) H1..H5 and steady sine40 (27.3-28.6 s) H1..H3, dB, of the
12 Resonance-0 / Depth-0 rows (T1/T2 x H 0,5,10,25,37,50; the 09-23 take where one exists; the
09-18 T2 H0 R0 row is replaced by its 09-23 take2, see the report). Levels below -108 dBFS in the
capture are floor and are fitted as 'at or below -108'.
Held out: the 1 kHz ramp describing function (H1, H2, H3 at 12 levels from -39 to -1 dBFS).
Budget fixed in advance: scipy least_squares, max_nfev 4000.
"""
import sys, os, json
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from q import *
from scipy.signal import lfilter
from scipy.optimize import least_squares
from static_law import lp2_mag

H2d = os.path.dirname(os.path.abspath(__file__))
FSD = 39062.5
xd = np.load(os.path.join(H2d, "xd_LIN.npy"))


def seg(a, b):
    return xd[int(a * FSD):int(b * FSD)]


S20 = seg(22.4, 25.0)
S40 = seg(27.0, 28.6)
HP_A = np.exp(-2 * np.pi * 5.0 / FSD)


def rbj(f0, Q):
    w0 = 2 * np.pi * f0 / FSD
    al = np.sin(w0) / (2 * Q)
    c = np.cos(w0)
    b = np.array([(1 - c) / 2, 1 - c, (1 - c) / 2])
    a = np.array([1 + al, -2 * c, 1 - al])
    return b / a[0], a / a[0]


RES = rbj(389.0, 4.13)
FIX = rbj(5706.0, 1.92)


def drive(x, g, wo, ge, we):
    y = wo * np.clip(g * x, -1, 1)
    if we:
        r = np.clip(np.abs(ge * x), 0, 1)
        r = lfilter([1, -1], [1, -HP_A], r)
        y = y + we * r
    return y


def post(y):
    y = lfilter(*RES, y)
    return lfilter(*FIX, y)


def harm(y, t_skip, nh, f0=1000.0):
    y = y[int(t_skip * FSD):]
    n = len(y)
    w = np.hanning(n)
    Y = np.abs(np.fft.rfft(y * w)) / (np.sum(w) / 2)
    out = []
    for k in range(1, nh + 1):
        i = int(round(f0 * k * n / FSD))
        out.append(20 * np.log10(max(Y[i - 3:i + 4].max(), 1e-14)))
    return np.array(out)


HS = [0, 5, 10, 25, 37, 50]


def row_name(T, H):
    c = [n for n in find(Type=T, Harmonics=H, Depth=0, Resonance=0, DirectLevel=0, EffectLevel=50) if P(n)["in"] == "LIN"]
    newer = [n for n in c if P(n)["new"]]
    return (newer or c)[-1]


ROWS = [(T, H, row_name(T, H)) for T in (1, 2) for H in HS]
TARGET = {}
for T, H, n in ROWS:
    TARGET[(T, H)] = (np.array(F[n]["h_sine20"][:5]), np.array(F[n]["h_sine40"][:3]))

FLOOR = -108.0


def unpack(v):
    # per (T,H): log g, logit-ish w_odd (as dB); per H: log ge, we (dB); shared gain dB
    i = 0
    odd = {}
    for T in (1, 2):
        for H in HS:
            odd[(T, H)] = (v[i], v[i + 1]); i += 2
    ev = {}
    for H in HS:
        ev[H] = (v[i], v[i + 1]); i += 2
    gain = v[i]
    return odd, ev, gain


def model_h(odd, ev, gain, T, H, xs=S20, t_skip=0.6, nh=5):
    gdb, wodb = odd[(T, H)]
    gedb, wedb = ev[H]
    y = drive(xs, 10 ** (gdb / 20), 10 ** (wodb / 20), 10 ** (gedb / 20), 10 ** (wedb / 20))
    y = post(y) * 10 ** (gain / 20)
    return harm(y, t_skip, nh)


from pyguard import quiet


def resid(v):
    quiet()
    odd, ev, gain = unpack(v)
    r = []
    for T, H, n in ROWS:
        t20, t40 = TARGET[(T, H)]
        m20 = model_h(odd, ev, gain, T, H, S20, 0.6, 5)
        m40 = model_h(odd, ev, gain, T, H, S40, 0.3, 3)
        for m, t in ((m20, t20), (m40, t40)):
            e = m - t
            fl = t < FLOOR
            e = np.where(fl, np.maximum(m - FLOOR, 0.0), e)   # at/below floor: only penalise if model is above it
            r.append(e)
    return np.concatenate(r)


def x_start():
    v = []
    old = json.load(open("/path/to/ax30g/models/ax30g-hypr.json"))["blocks"]["driver"]["table"]

    def interp(pts, h):
        pts = sorted(pts)
        return float(np.interp(h, [p[0] for p in pts], [p[1] for p in pts]))
    for T in (1, 2):
        for H in HS:
            t = old[str(T)]
            v += [interp(t["gain_db"], H), 20 * np.log10(max(interp(t["w_odd"], H), 1e-3))]
    for H in HS:
        v += [interp(old["1"]["gain_db"], H) if H else 20.0, 20 * np.log10(max(interp(old["1"]["w_even"], H), 1e-3))]
    v += [-18.96]
    return np.array(v)


if __name__ == "__main__":
    x0 = x_start()
    r0 = resid(x0)
    print("start rms %.2f dB over %d targets" % (np.sqrt(np.mean(r0 ** 2)), len(r0)), flush=True)
    best = None
    for trial, jit in enumerate((0.0, 6.0, 12.0)):
        rng = np.random.default_rng(trial)
        xs = x0 + rng.normal(0, jit, len(x0)) * (jit > 0)
        r = least_squares(resid, xs, method="trf", loss="soft_l1", f_scale=3.0, max_nfev=1300, diff_step=1e-3)
        rr = resid(r.x)
        print("trial %d: rms %.2f dB (nfev %d)" % (trial, np.sqrt(np.mean(rr ** 2)), r.nfev), flush=True)
        if best is None or np.sqrt(np.mean(rr ** 2)) < best[0]:
            best = (np.sqrt(np.mean(rr ** 2)), r.x)
    odd, ev, gain = unpack(best[1])
    out = {"rms_db": best[0], "odd": {"%d,%d" % k: v for k, v in odd.items()}, "even": {str(k): v for k, v in ev.items()}, "gain_db": gain}
    json.dump(out, open(os.path.join(H2d, "driver_fit.json"), "w"), indent=1, default=float)
    print(json.dumps(out, indent=1, default=float))
    # per-row table
    for T, H, n in ROWS:
        t20, t40 = TARGET[(T, H)]
        m20 = model_h(odd, ev, gain, T, H, S20, 0.6, 5)
        print("T%d H%-2d  capture %s  model %s" % (T, H, np.round(t20, 1), np.round(m20, 1)))
