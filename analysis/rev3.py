"""REV pass-3 tooling: the High Damp filter, and a renderer with a per-comb
in-loop lowpass.

`analysis/rev2.py` (pass 2) established the tank: four parallel feedback combs,
per-channel read positions, a three-section negative-coefficient allpass
cascade per channel (`docs/rev-model-2026-09-18.md`). It left the in-loop High
Damp filter fitted at 1 kHz only, with one pole per Type.

This module adds

  * `undiffuse()` -- the exact inverse of a Schroeder allpass cascade, run
    anticausally (reverse, same cascade, reverse), which turns a measured
    device-rate impulse response back into the comb tap train that fed the
    cascade;
  * `per_pass()` -- the per-pass response of one comb, measured directly as
    the ratio of the spectrum of the k-th loop return to the 0-th, windowed
    on the un-diffused tap train. This is model-free: the tap template
    (resampling kernel + whatever the chain deconvolution leaves) is common
    to both windows and divides out;
  * `fit_*()` -- candidate first/second-order forms for that response;
  * `comb_train()` and `fit_poles_ir()` -- a fit by simulation of the whole
    un-diffused tap train, for the Types whose taps are too close together
    to window individually (ROOM, PLATE);
  * `render_rev3()` -- `engine.effects.render_rev` with ONE change: the
    in-loop lowpass pole is per comb, scaled by that comb's `rt_length`
    (`maps.high_damp` gives the longest comb's pole). `install()` points
    `engine.render` at it, so `models/ax30g-rev-hd-2026-09-18.json` renders
    without editing anything under `engine/`.

`engine/effects.py`, `models/ax30g-rev.json` and `tests/rev_null.py` are left
exactly as pass 2 wrote them; a C++ port was reading them while this was
measured.
"""
import os
import sys
import itertools
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)

import analysis.rev2 as R2                                          # noqa: E402
from analysis.rev2 import FS_DEV                                    # noqa: E402
from engine.effects import _rev_allpass, _rev_comb, _shift, _rev_type_table   # noqa: E402
from engine.params import apply_map                                 # noqa: E402

CAP_DIR = R2.CAP_DIR


# ------------------------------------------------------------------ structure

def undiffuse(x, aps):
    """Invert a series Schroeder allpass cascade y = AP(L1,u1)...AP(Ln,un) x.

    Each section is y[n] = u*v[n] + v[n-L] - u*y[n-L], i.e.
    H(z) = (u + z^-L)/(1 + u z^-L), whose poles sit OUTSIDE the unit circle,
    so the stable inverse is anticausal. Written out in reversed time the
    inverse recursion is the *same* difference equation, so reversing,
    running the cascade and reversing back is exact.
    """
    y = np.asarray(x, dtype=float)[::-1].copy()
    for L, u in aps:
        y = _rev_allpass(y, int(L), float(u))
    return y[::-1].copy()


def tap_positions(T, ch, kmax=4):
    """Every comb tap (position relative to the input delay, comb index, loop
    number) out to loop kmax."""
    return [(r[ch] + k * d, i, k)
            for i, (d, r) in enumerate(zip(T["combs"], T["reads"]))
            for k in range(kmax + 1)]


def comb_train(T, ch, gains, poles, n, input_delay=True):
    """The tank's impulse response at one output channel BEFORE the allpass
    cascade: each comb driven by a unit impulse, read at that channel's
    offset, summed at unit weight."""
    delta = np.zeros(n)
    delta[0] = 1.0
    out = np.zeros(n)
    for i, (d, r) in enumerate(zip(T["combs"], T["reads"])):
        c = _rev_comb(delta, int(d), float(gains[i]), float(poles[i]))
        out += _shift(c, int(r[ch]), n)
    if input_delay:
        out = _shift(out, int(T.get("input_delay", [0, 0])[ch]), n)
    return out


def comb_gains(T, rev_time, fs=FS_DEV):
    rtl = T.get("rt_length", T["combs"])
    return [min(10.0 ** (-3.0 * L / (max(rev_time, 1e-6) * fs)), 0.9999) for L in rtl]


# ------------------------------------------------------- per-pass measurement

FREQS = np.array([125., 177., 250., 354., 500., 707., 1000., 1414., 2000.,
                  2828., 4000., 5657., 8000., 11314., 16000.])


def _ratio(u, p0, p1, hw, nfft=8192):
    w = np.hanning(2 * hw + 1)
    A = np.fft.rfft(u[p0 - hw:p0 + hw + 1] * w, nfft)
    B = np.fft.rfft(u[p1 - hw:p1 + hw + 1] * w, nfft)
    return np.fft.rfftfreq(nfft, 1.0 / FS_DEV), B / np.where(np.abs(A) < 1e-15, 1e-15, A)


def _base(T, ch, u, i0, search=800):
    """Index in `u` at which the tank's own t = 0 sits (the capture's
    predelay and the chain's latency are absorbed), found from the earliest
    tap the structure predicts."""
    first = T.get("input_delay", [0, 0])[ch] + min(r[ch] for r in T["reads"])
    k = int(np.argmax(np.abs(u[i0:i0 + search])))
    return i0 + k - first


def per_pass(typ, high_damp, rev_time=10.0, balance=50, pre_dly=1, take=None,
             which="rev", model=None, kmax=2, hw_min=100, hw_max=384,
             freqs=FREQS, segment="clicks", dur_s=2.0):
    """Per-pass response of each comb, in dB, measured directly.

    For every (comb, channel) and every pair of loop numbers ka < kb whose
    taps are isolated enough to window, the ratio of the two windowed spectra
    is (g*H)^(kb-ka). Returns {comb delay: dict(curve_db, n, spread_db,
    pairs)} with the *filter* part alone (the comb's own Rev Time gain
    divided out) averaged over the usable pairs.
    """
    import json
    model = model or os.path.join(ROOT, "models", "ax30g-rev.json")
    with open(model) as f:
        M = json.load(f)
    T = M["types"][typ]
    fn = R2.name(typ, rev_time, Balance=balance, HighDamp=high_damp,
                 PreDly=pre_dly, take=take, which=which)
    i0, h, info = R2.ir(fn, segment=segment, dur_s=dur_s)
    rtl = T.get("rt_length", T["combs"])
    acc = {}
    for ch in (0, 1):
        aps = T["allpass_l"] if ch == 0 else T["allpass_r"]
        u = undiffuse(h[:, ch], aps)
        base = _base(T, ch, u, i0)
        taps = tap_positions(T, ch, kmax=kmax + 2)
        for i, d in enumerate(T["combs"]):
            o = T["reads"][i][ch]
            for ka, kb in itertools.combinations(range(kmax + 1), 2):
                pa, pb = o + ka * d, o + kb * d
                ia = min(abs(p - pa) for p, j, kk in taps if (j, kk) != (i, ka))
                ib = min(abs(p - pb) for p, j, kk in taps if (j, kk) != (i, kb))
                hw = int(min(hw_max, min(ia, ib) // 2 - 12))
                if hw < hw_min:
                    continue
                off = T.get("input_delay", [0, 0])[ch]
                fr, R = _ratio(u, base + off + pa, base + off + pb, hw)
                db = 20 * np.log10(np.abs(R) + 1e-15) / (kb - ka)
                g_db = 20 * np.log10(10.0 ** (-3.0 * rtl[i] / (rev_time * FS_DEV)))
                acc.setdefault(d, []).append(
                    (np.interp(freqs, fr, db) - g_db, ch, ka, kb, hw))
    out = {}
    for d, rows in acc.items():
        cur = np.array([r[0] for r in rows])
        out[d] = {"curve_db": np.median(cur, axis=0), "n": len(rows),
                  "spread_db": (np.percentile(cur, 84, axis=0) - np.percentile(cur, 16, axis=0)) / 2,
                  "pairs": [(r[1], r[2], r[3], r[4]) for r in rows],
                  "all": cur}
    return out, info


# ------------------------------------------------------------- filter forms

def _z(f, fs=FS_DEV):
    return np.exp(-2j * np.pi * np.asarray(f, float) / fs)


def onepole_db(a, f, dc=1.0):
    """w[n] = (1-a) y[n] + a w[n-1], optionally with a fixed DC gain."""
    return 20 * np.log10(np.abs(dc * (1 - a) / (1 - a * _z(f))) + 1e-30)


def shelf1_db(a, b1, f):
    """First-order shelf (1 + b1 z^-1)/(1 - a z^-1), normalised to unity DC."""
    H = (1 + b1 * _z(f)) / (1 - a * _z(f))
    H0 = (1 + b1) / (1 - a)
    return 20 * np.log10(np.abs(H / H0) + 1e-30)


def twopole_db(a1, a2, f):
    """Two real poles in series, each unity at DC."""
    z = _z(f)
    H = ((1 - a1) / (1 - a1 * z)) * ((1 - a2) / (1 - a2 * z))
    return 20 * np.log10(np.abs(H) + 1e-30)


def fit_form(form, curve_db, freqs=FREQS, fmax=8000.0):
    """Least-squares fit of one candidate form to a measured per-pass curve.
    Returns (params, rms_db)."""
    from scipy.optimize import least_squares, minimize_scalar
    sel = freqs <= fmax
    f, y = freqs[sel], np.asarray(curve_db)[sel]
    if form == "onepole":
        r = minimize_scalar(lambda a: float(np.mean((onepole_db(a, f) - y) ** 2)),
                            bounds=(0.0, 0.999), method="bounded")
        return (r.x,), float(np.sqrt(r.fun))
    if form == "onepole_dc":
        sol = least_squares(lambda p: onepole_db(p[0], f, 10 ** (p[1] / 20)) - y,
                            [0.5, 0.0], bounds=([0.0, -6.0], [0.999, 6.0]))
        return tuple(sol.x), float(np.sqrt(np.mean(sol.fun ** 2)))
    if form == "shelf":
        sol = least_squares(lambda p: shelf1_db(p[0], p[1], f) - y,
                            [0.5, 0.0], bounds=([-0.999, -0.999], [0.999, 0.999]))
        return tuple(sol.x), float(np.sqrt(np.mean(sol.fun ** 2)))
    if form == "twopole":
        sol = least_squares(lambda p: twopole_db(p[0], p[1], f) - y,
                            [0.4, 0.1], bounds=([0.0, 0.0], [0.999, 0.999]))
        return tuple(sol.x), float(np.sqrt(np.mean(sol.fun ** 2)))
    raise ValueError(form)


# --------------------------------------------------- fit by simulation (IR)

def fit_poles_ir(typ, high_damp, rev_time=10.0, balance=50, take=None,
                 which="rev", model=None, n=20000, law=None):
    """Fit the in-loop poles against the whole un-diffused tap train.

    `law=None` fits four independent poles; `law="rt_length"` fits the single
    number A with pole_i = A * rt_length_i / max(rt_length). Returns
    (poles, A_or_None, residual_db, undamped_residual_db).
    """
    import json
    from scipy.optimize import least_squares, minimize_scalar
    model = model or os.path.join(ROOT, "models", "ax30g-rev.json")
    with open(model) as f:
        M = json.load(f)
    T = M["types"][typ]
    fn = R2.name(typ, rev_time, Balance=balance, HighDamp=high_damp, take=take, which=which)
    i0, h, _ = R2.ir(fn, segment="clicks", dur_s=2.0)
    gs = comb_gains(T, rev_time)
    rtl = np.array(T.get("rt_length", T["combs"]), float)
    meas, tmpl = {}, {}
    for ch in (0, 1):
        aps = T["allpass_l"] if ch == 0 else T["allpass_r"]
        u = undiffuse(h[:, ch], aps)
        base = _base(T, ch, u, i0)
        first = T.get("input_delay", [0, 0])[ch] + min(r[ch] for r in T["reads"])
        meas[ch] = u[base:base + n]
        tmpl[ch] = R2.tap_template(u, base, first, half=8, n=32)

    def resid(poles):
        out = []
        for ch in (0, 1):
            t, k0 = tmpl[ch]
            mdl = np.convolve(comb_train(T, ch, gs, poles, n), t)[k0:k0 + n]
            m = meas[ch]
            g = np.dot(mdl, m) / max(np.dot(mdl, mdl), 1e-30)
            out.append(m - g * mdl)
        return np.concatenate(out)

    ref = float(np.sqrt(np.mean(np.concatenate([meas[0], meas[1]]) ** 2)))
    def db(r):
        return 20 * np.log10(np.sqrt(np.mean(r ** 2)) / ref)
    und = db(resid(np.zeros(4)))
    if high_damp == 0:
        return np.zeros(4), 0.0, und, und
    if law == "rt_length":
        r = minimize_scalar(lambda A: float(np.mean(resid(A * rtl / rtl.max()) ** 2)),
                            bounds=(0.0, 0.999), method="bounded")
        A = float(r.x)
        return A * rtl / rtl.max(), A, db(resid(A * rtl / rtl.max())), und
    sol = least_squares(resid, np.full(4, 0.3), bounds=(np.zeros(4), np.full(4, 0.995)),
                        xtol=1e-11, ftol=1e-11)
    return sol.x, None, db(resid(sol.x)), und


# --------------------------------------------------------------- renderer

def rev_poles(spec, T, fs):
    """The four in-loop lowpass poles for the current settings.

    `high_damp_scaling: "rt_length"` (this model): `maps.high_damp` gives the
    pole of the comb with the LONGEST rt_length and every other comb's pole
    is that scaled by its own rt_length. Anything else falls back to pass 2's
    single per-Type pole, so an old spec renders unchanged.
    """
    hd = float(spec["params"]["High Damp"])
    rtl = np.array(T.get("rt_length", T["combs"]), float)
    scaling = T.get("high_damp_scaling", spec.get("high_damp_scaling"))
    if scaling == "rt_length":
        A = float(apply_map(spec["maps"]["high_damp"], hd, fs))
        A = min(max(A, 0.0), float(spec.get("high_damp_max_pole", 0.995)))
        return A * rtl / rtl.max()
    a = float(apply_map(T.get("high_damp_map", spec["maps"]["high_damp"]), hd, fs))
    return np.full(4, a)


def render_rev3(x, spec, fs):
    """`engine.effects.render_rev` with a PER-COMB in-loop lowpass.

    Identical to pass 2's renderer in every other respect; with a pass-2 spec
    (one pole per Type) it reproduces it bit for bit, because `rev_poles`
    falls back to the same scalar.
    """
    p = spec["params"]
    m = spec["maps"]
    tname, T = _rev_type_table(spec)
    n = x.shape[0]
    mono = 0.5 * (x[:, 0] + x[:, 1]) if spec.get("input", "sum") == "sum" else x[:, 0]

    pre = int(round(apply_map(m["pre_delay"], p["Pre Dly"], fs)))
    poles = rev_poles(spec, T, fs)
    wet = float(apply_map(m["balance_wet"], p["Balance"], fs))
    dry = float(apply_map(m["balance_dry"], p["Balance"], fs))
    pol = float(T.get("wet_polarity", spec.get("wet_polarity", -1.0)))
    ing = float(T.get("input_gain", 1.0))
    mix = float(T.get("comb_mix", spec.get("comb_mix", 0.5)))

    combs = [int(v) for v in T["combs"]]
    reads = [[int(a), int(b)] for a, b in T["reads"]]
    ind = [int(v) for v in T.get("input_delay", [0, 0])]
    gs = comb_gains(T, float(p["Rev Time"]), fs)

    pad = pre + max(ind) + max(max(r) for r in reads) + max(combs)
    N = n + pad + int(0.2 * fs)
    src = _shift(mono, pre, N)

    outs = []
    combouts = [_rev_comb(src, d, g, a) for d, g, a in zip(combs, gs, poles)]
    for ch in (0, 1):
        v = np.zeros(N)
        for co, r in zip(combouts, reads):
            v += mix * _shift(co, ind[ch] + r[ch], N)
        aps = T["allpass_l"] if ch == 0 else T["allpass_r"]
        for L, u in aps:
            v = _rev_allpass(v, int(L), float(u))
        outs.append(pol * ing * v[:n])

    y = np.stack([dry * mono + wet * outs[0], dry * mono + wet * outs[1]], axis=1)
    truth = {"effect": "REV", "type": tname, "pre_delay_samples": pre,
             "combs": combs, "comb_gains": gs, "reads": reads,
             "input_delay": ind, "comb_mix": mix,
             "damp_poles": [float(v) for v in poles],
             "allpass_l": T["allpass_l"], "allpass_r": T["allpass_r"],
             "wet": wet, "dry": dry, "input_gain": ing}
    return y, truth


def install():
    """Point `engine.render` at `render_rev3` for REV, leaving
    `engine/effects.py` untouched on disk."""
    import engine.render as R
    R.RENDERERS["REV"] = render_rev3
    return R.RENDERERS


# ------------------------------------------------------------------- nulling

def null_capture(model, capname, segments=("clicks", "sweep", "burst_hot"),
                 skip_s=0.0, limit_s=None, channels=(0, 1)):
    """One capture against one model spec, per segment, in dB.

    Same contract as `tests/rev_null.py` (one fractional lag and one gain
    fitted on the segment reported), but it installs `render_rev3` first so a
    per-comb High Damp spec renders.
    """
    import re
    from analysis.util import seg
    from engine.render import load_spec, render_spec
    install()
    spec = load_spec(model)
    mm = re.match(r"AX30G_REV_HighDamp-(?P<hd>[\d.]+)_Balance-(?P<bal>[\d.]+)_Type-(?P<type>\w+)"
                  r"_PreDly-(?P<pd>[\d.]+)_RevTime-(?P<rt>[\d.]+)_IN-LIN_SET-(?P<set>\w+?)"
                  r"(?:_take(?P<take>\d+))?\.wav", os.path.basename(capname))
    d = mm.groupdict()
    spec["params"] = {"Type": d["type"], "Pre Dly": float(d["pd"]),
                      "Rev Time": float(d["rt"]), "High Damp": float(d["hd"]),
                      "Balance": float(d["bal"])}
    which = d["set"]
    x, fs_sig = R2.signal(which)
    lay = R2.layout(which)
    y, _ = render_spec(spec, x, fs_sig, spec_path=model)
    cap, fs, info, _ = R2.load(capname)
    out = {}
    for s in [q["name"] for q in lay["segments"] if q["name"] in segments]:
        sg = seg(lay, s)
        i0 = int((sg["start"] + skip_s) * fs)
        i1 = int(sg["end"] * fs) if limit_s is None else int((sg["start"] + skip_s + limit_s) * fs)
        i1 = min(i1, cap.shape[0], y.shape[0])
        out[s] = float(np.mean([R2.null_fast(cap[i0:i1, c], y[i0:i1, c])[0] for c in channels]))
    return out
