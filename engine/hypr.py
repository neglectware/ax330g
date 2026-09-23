"""HYPR -- the AX30G's Block-1 Hyper Resonator (mono).

Measured 2026-09-18 from the 21-row grid `capture/grids/hypr.json`; the
derivation, every candidate with its residual and what is still
under-determined are in `docs/hypr-model-2026-09-18.md`.

The block in one line:

    out = Direct * x  +  Effect * RES( DRIVER(x), f(E) )

with three measured parts:

* **DRIVER** -- a static waveshaper: `u = g*x`, then a mix of a symmetric
  hard clip (odd harmonics) and a DC-blocked full-wave rectifier (even
  harmonics).  `g` and the two mix weights are per (Type, Harmonics); the
  measured grid has three Harmonics points per Type.  Type 1 / Harmonics 0
  is nearly linear and 50 dB down, Type 1 / Harmonics 50 is almost purely
  even (a frequency doubler), Type 2 is clip-dominated at every Harmonics.

* **RES** -- the resonator: a 2-pole resonant low-pass whose Q is set by
  Resonance and whose corner is swept by the envelope, cascaded with a
  second, fixed 2-pole low-pass.  At rest the corner is 389 Hz (measured
  three independent ways to 0.2 %).

* **the sweep** -- a peak envelope follower drives the corner in octaves
  above (Polarity UP) or below (DOWN) the rest frequency, with a threshold:
  below about -34 dBFS at the device the corner does not move at all.

The block runs in the PRE-EMPHASISED domain and, unlike every other block
measured so far, its output is NOT de-emphasised: the Direct path alone
measures exactly the input stage's pre-emphasis shelf above the Bypass
capture (+0.5 dB at 1.1 kHz, +3.0 at 2.8 k, +5.1 at 4.4 k, +7.1 at 7.0 k,
+8.5 at 11.1 k, +9.2 at 17.6 k, against the shelf's +0.1/+2.5/+4.6/+6.7/
+8.4/+9.5), on all three of the noise, sweep and DI segments.  So the spec
carries `input_stage.de_emphasis = "after_effect"` (which makes
engine/render.py defer the de-emphasis filter to after the renderer) AND
`blocks.output_de_emphasis = "none"`, which makes this module re-apply the
pre-emphasis at its output so that the deferred filter cancels exactly.

This module deliberately does NOT live in engine/effects.py beyond a
one-line dispatch entry: that file was owned by other sessions on
2026-09-18.
"""
import math
import numpy as np
from scipy.signal import lfilter

from .params import apply_map


# ----------------------------------------------------------------- helpers
def _interp_pairs(pts, v):
    pts = sorted(pts, key=lambda p: p[0])
    if v <= pts[0][0]:
        return float(pts[0][1])
    for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
        if x0 <= v <= x1:
            t = 0.0 if x1 == x0 else (v - x0) / (x1 - x0)
            return float(y0 + (y1 - y0) * t)
    return float(pts[-1][1])


def _tau_coef(tau_ms, fs):
    if not tau_ms or tau_ms <= 0:
        return 1.0
    return 1.0 - math.exp(-1.0 / (tau_ms * 1e-3 * fs))


def peak_envelope(x, aa, ar):
    """One-pole peak follower on |x| -- the same detector shape the
    compressor was measured to use (docs/comp-model-2026-09-18.md)."""
    e = 0.0
    out = np.empty(len(x), dtype=float)
    ax = np.abs(x)
    for i in range(len(ax)):
        v = ax[i]
        e += (aa if v > e else ar) * (v - e)
        out[i] = e
    return out


def _shelf_ba(fs, st):
    """The input stage's pre-emphasis shelf as (b, a) at rate fs."""
    from .render import shelf1_coeffs
    pe = (st or {}).get("pre_emphasis") or {}
    if not pe.get("tau_zero_us"):
        return None
    return shelf1_coeffs(fs, pe["tau_zero_us"] * 1e-6, pe["tau_pole_us"] * 1e-6)


# ------------------------------------------------------------------ driver
def driver(x, g, w_odd, w_even, clip_level=1.0, rect_hpf_hz=None, fs=39062.5, g_even=None,
           odd_square=None):
    """The harmonic driver: gain, then a mix of a symmetric hard clip and a
    DC-blocked full-wave rectifier.

    Both branches clip at the same ceiling, which is what the MAX capture
    says (the driver's own ceiling is reached long before the converter's).
    The rectifier's DC is removed by a first-order high-pass, not by
    subtracting the mean, so a changing envelope behaves the way the unit's
    does.

    `g_even` (2026-09-23, models/ax30g-hypr-2.json): the even branch's own
    gain. The hypr-2 captures give the same H2/H4 on Type 1 and Type 2 at
    every Harmonics setting, so the even branch is fed from the input with a
    gain of its own rather than from the Type's odd-branch gain. None keeps
    the 2026-09-18 behaviour (both branches share `g`).

    `odd_square` (2026-09-23): (knee, release_ms). The odd branch is then a
    square wave whose amplitude follows the input's peak envelope up to the
    knee, y = sign(x) * min(E, knee) / knee -- which is what Type 2
    measures: the 1 kHz output rises 1:1 and then stops, and H3/H1 is the
    same (-29 to -30 dB) at every level of the ramp, below the knee too,
    which a plain hard clip cannot do."""
    u = g * np.asarray(x, dtype=float)
    y = 0.0
    if w_odd and odd_square:
        knee, rel = odd_square
        xx = np.asarray(x, dtype=float)
        E = _decaying_peak(xx, rel, fs)
        y = y + w_odd * np.sign(xx) * np.minimum(E, knee) / knee
    elif w_odd:
        y = y + w_odd * np.clip(u, -clip_level, clip_level)
    if w_even:
        ue = u if g_even is None else g_even * np.asarray(x, dtype=float)
        r = np.clip(np.abs(ue), 0.0, clip_level)
        if rect_hpf_hz:
            a = math.exp(-2.0 * math.pi * rect_hpf_hz / fs)
            r = lfilter([1.0, -1.0], [1.0, -a], r)
        else:
            r = r - float(np.mean(r))
        y = y + w_even * r
    return y


# --------------------------------------------------------------- resonator
def _lp2(f0, Q, fs):
    """RBJ low-pass biquad, unity at DC, peak height ~= Q."""
    w0 = 2.0 * math.pi * min(f0, 0.45 * fs) / fs
    alpha = math.sin(w0) / (2.0 * max(Q, 0.05))
    c = math.cos(w0)
    b0 = (1 - c) / 2.0
    b1 = 1 - c
    b2 = b0
    a0 = 1 + alpha
    a1 = -2 * c
    a2 = 1 - alpha
    return (b0 / a0, b1 / a0, b2 / a0), (a1 / a0, a2 / a0)


def sweeping_lp2(x, f0_track, Q, fs, block=1):
    """A 2-pole low-pass whose corner follows `f0_track` (one value per
    sample).  Coefficients are recomputed every `block` samples -- the
    device updates its filter word at a control rate, and `block` is the
    model's handle on it."""
    n = len(x)
    y = np.empty(n)
    z1 = z2 = 0.0
    i = 0
    while i < n:
        j = min(i + block, n)
        (b0, b1, b2), (a1, a2) = _lp2(float(f0_track[i]), Q, fs)
        for k in range(i, j):
            v = x[k] - a1 * z1 - a2 * z2
            y[k] = b0 * v + b1 * z1 + b2 * z2
            z2 = z1
            z1 = v
        i = j
    return y


# ------------------------------------------- trigger-EG sweep (2026-09-23)
def _decaying_peak(x, tau_ms, fs):
    """E[n] = max(|x[n]|, a*E[n-1]) -- a peak follower with instant attack
    and exponential release, computed exactly without a Python loop:
    log E[n] = n*log a + cummax(log|x[k]| - k*log a)."""
    a = math.exp(-1.0 / (tau_ms * 1e-3 * fs))
    la = math.log(a)
    n = np.arange(len(x))
    lx = np.log(np.maximum(np.abs(x), 1e-12))
    # in chunks, so k*log(a) stays well inside float range on long files;
    # the previous chunk's last value enters as E_prev*a at k = 0
    out = np.empty(len(x))
    carry = -np.inf                      # log(E_prev) + log(a)
    step = 1 << 16
    for s in range(0, len(x), step):
        k = n[s:s + step] - s
        m = np.maximum.accumulate(lx[s:s + step] - k * la)
        m = np.maximum(m, carry)
        out[s:s + step] = m + k * la
        carry = out[s + len(k) - 1] + la
    return np.exp(out)


def _trigger_times(E, on_db, hyst_db):
    """Schmitt trigger on the detector: fire when armed and E >= on; re-arm
    when E drops below on - hyst."""
    on = 10.0 ** (on_db / 20.0)
    off = 10.0 ** ((on_db - hyst_db) / 20.0)
    above = E >= on
    below = E < off
    fires, i, armed = [], 0, True
    while True:
        idx = np.flatnonzero(above[i:] if armed else below[i:])
        if not len(idx):
            break
        i += int(idx[0])
        if armed:
            fires.append(i)
        armed = not armed
    return np.array(fires, dtype=int)


def _eg(n, fires, tau_att_ms, tau_dec_ms, t_sw_ms, fs):
    """Attack toward 1 (one-pole, tau_att) for t_sw after each fire, from
    the current value; then exponential decay toward 0 with tau_dec."""
    g = np.zeros(n)
    ta = max(tau_att_ms, 1e-3) * 1e-3 * fs
    td = max(tau_dec_ms, 1e-3) * 1e-3 * fs
    tsw = int(round(t_sw_ms * 1e-3 * fs))
    cur = 0.0
    for j, s in enumerate(fires):
        e = fires[j + 1] if j + 1 < len(fires) else n
        k = np.arange(e - s)
        a_len = min(tsw, e - s)
        seg = np.empty(e - s)
        seg[:a_len] = 1.0 - (1.0 - cur) * np.exp(-k[:a_len] / ta)
        if e - s > a_len:
            gp = seg[a_len - 1] if a_len > 0 else cur
            seg[a_len:] = gp * np.exp(-(k[a_len:] - a_len + 1) / td)
        g[s:e] = seg
        cur = seg[-1]
    return g


def trigger_eg_track(mono, spec, fs):
    """The 2026-09-23 sweep: a Schmitt-triggered attack/decay envelope, not
    an envelope follower. Returns (f_track, info)."""
    p = spec["params"]
    m = spec.get("maps", {})
    sw = spec["blocks"]["sweep"]
    S = float(p.get("Sensitivity", 25))
    D = float(p.get("Depth", 0))
    DEC = float(p.get("Decay", 25))
    POL = str(p.get("Polarity", "UP")).upper()
    tr = sw["trigger"]
    E = _decaying_peak(mono, float(tr["detector_release_ms"]), fs)
    hyst = _interp_pairs(m["sens_hysteresis_db"]["points"], S)
    fires = _trigger_times(E, float(tr["threshold_dbfs"]), hyst)
    eg = sw["eg"]
    tdec = math.exp(_interp_pairs([[a, math.log(b)] for a, b in m["decay_tau_ms"]["points"]], DEC))
    g = _eg(len(mono), fires, float(eg["attack_ms"]), tdec, float(eg["attack_len_ms"]), fs)
    Fr = 2.0 * math.sin(math.pi * float(sw["rest_hz"]) / fs)
    if POL == "DOWN":
        Fv = 2.0 * math.sin(math.pi * float(sw["down_rest_hz"]) / fs) - float(sw["F_per_depth_down"]) * D * g
    else:
        Fv = Fr + float(sw["F_per_depth_up"]) * D * g
    Fv = np.clip(Fv, 2.0 * math.sin(math.pi * float(sw.get("min_hz", 40.0)) / fs),
                 2.0 * math.sin(math.pi * min(float(sw.get("max_hz", 15000.0)), 0.45 * fs) / fs))
    f = fs / math.pi * np.arcsin(np.clip(Fv / 2.0, 0.0, 1.0))
    return f, {"n_triggers": int(len(fires)), "hysteresis_db": hyst, "decay_tau_ms": tdec}


def _q_of(m, R):
    q = m.get("q")
    if not q:
        return 4.28
    if q.get("interp") == "inverse":
        # damping (1/Q) interpolated, which is what the hypr-2 table is linear in
        return 1.0 / _interp_pairs([[a, 1.0 / b] for a, b in q["points"]], R)
    return _interp_pairs(q["points"], R)


def _envelope_sweep(mono, d, spec, fs, truth):
    """The 2026-09-18 sweep: an envelope follower mapped to octaves above a
    threshold. Kept verbatim for models/ax30g-hypr.json."""
    p = spec["params"]
    m = spec.get("maps", {})
    b_ = spec.get("blocks", {})
    S = float(p.get("Sensitivity", 25))
    POL = str(p.get("Polarity", "UP")).upper()
    D = float(p.get("Depth", 0))
    DEC = float(p.get("Decay", 25))
    # ---- envelope -------------------------------------------------------
    det = b_.get("detector", {})
    src = det.get("source", "input")
    sig = mono if src == "input" else d
    ta = float(apply_map(m["attack_tau_ms"], DEC, fs)) if "attack_tau_ms" in m else float(det.get("attack_tau_ms", 1.2))
    tr = float(apply_map(m["release_tau_ms"], DEC, fs)) if "release_tau_ms" in m else float(det.get("release_tau_ms", 48.0))
    E = peak_envelope(sig, _tau_coef(ta, fs), _tau_coef(tr, fs))
    truth.update({"attack_tau_ms": ta, "release_tau_ms": tr, "detector_source": src})

    # ---- the sweep: envelope -> corner ---------------------------------
    sw = b_.get("sweep", {})
    f_rest = float(sw.get("rest_hz", 389.0))
    thr_db = float(sw.get("threshold_dbfs", -34.0))
    oct_max = float(sw.get("max_octaves", 4.6))
    k_s = _interp_pairs(m["sens"]["points"], S) if "sens" in m else 1.0
    k_d = _interp_pairs(m["depth"]["points"], D) if "depth" in m else D / 50.0
    slope = float(sw.get("octaves_per_db", 0.80)) * k_s * k_d
    L = 20.0 * np.log10(np.maximum(E, 1e-9))
    octv = np.clip(slope * np.maximum(L - thr_db, 0.0), 0.0, oct_max)
    sign = -1.0 if POL == "DOWN" else 1.0
    f_track = f_rest * np.exp2(sign * octv)
    f_track = np.clip(f_track, float(sw.get("min_hz", 40.0)),
                      min(float(sw.get("max_hz", 12000.0)), 0.45 * fs))
    truth.update({"rest_hz": f_rest, "octaves_per_db": slope,
                  "f_min_track": float(f_track.min()), "f_max_track": float(f_track.max())})

    return f_track


# ------------------------------------------------------------------ render
def render_hypr(x, spec, fs):
    """Block 1 Hyper Resonator.  x is (N,2) at the device rate; the block is
    mono, so the detector and the whole effect path run on the mean of the
    two channels and the one result goes to both."""
    p = spec["params"]
    m = spec.get("maps", {})
    b_ = spec.get("blocks", {})

    T = int(p.get("Type", 1))
    H = float(p.get("Harmonics", 0))
    S = float(p.get("Sensitivity", 25))
    POL = str(p.get("Polarity", "UP")).upper()
    D = float(p.get("Depth", 0))
    DEC = float(p.get("Decay", 25))
    R = float(p.get("Resonance", 0))
    DIR = float(p.get("Direct Level", p.get("DirectLevel", 0)))
    EFF = float(p.get("Effect Level", p.get("EffectLevel", 50)))

    mono = np.asarray(x, dtype=float)
    if mono.ndim == 2:
        mono = mono.mean(axis=1)

    truth = {"effect": "HYPR", "Type": T, "Harmonics": H, "Sensitivity": S,
             "Polarity": POL, "Depth": D, "Decay": DEC, "Resonance": R,
             "Direct Level": DIR, "Effect Level": EFF}

    # ---- driver ---------------------------------------------------------
    dv = b_.get("driver", {})
    tbl = dv.get("table", {}).get(str(T)) or dv.get("table", {}).get(T)
    if tbl is None:
        raise KeyError(f"blocks.driver.table has no entry for Type {T}")
    g = 10.0 ** (_interp_pairs(tbl["gain_db"], H) / 20.0)
    w_odd = _interp_pairs(tbl["w_odd"], H)
    w_even = _interp_pairs(tbl["w_even"], H)
    g_even = None
    ev = dv.get("even")
    if ev:
        # 2026-09-23: one even branch for both Types, with its own gain
        g_even = 10.0 ** (_interp_pairs(ev["gain_db"], H) / 20.0)
        w_even = _interp_pairs(ev["w"], H)
    osq = None
    if tbl.get("odd_mode") == "square_env":
        osq = (10.0 ** (float(tbl["knee_dbfs"]) / 20.0), float(tbl["env_release_ms"]))
    d = driver(mono, g, w_odd, w_even,
               clip_level=float(dv.get("clip_level", 1.0)),
               rect_hpf_hz=dv.get("rect_hpf_hz"), fs=fs, g_even=g_even, odd_square=osq)
    truth.update({"drive_gain_db": 20 * math.log10(max(g, 1e-12)),
                  "w_odd": w_odd, "w_even": w_even})
    if g_even is not None:
        truth["even_gain_db"] = 20 * math.log10(max(g_even, 1e-12))

    sw = b_.get("sweep", {})
    if sw.get("mode") == "trigger_eg":
        # ---- 2026-09-23: triggered attack/decay sweep ----------------------
        f_track, info = trigger_eg_track(mono, spec, fs)
        truth.update(info)
        truth.update({"rest_hz": float(sw["rest_hz"]),
                      "f_min_track": float(f_track.min()), "f_max_track": float(f_track.max())})
    else:
        f_track = _envelope_sweep(mono, d, spec, fs, truth)

    # ---- resonator ------------------------------------------------------
    rs = b_.get("resonator", {})
    Q = _q_of(m, R)
    blk = int(rs.get("control_block", 1))
    w = sweeping_lp2(d, f_track, Q, fs, block=blk)
    f1 = rs.get("fixed_lp_hz")
    if f1:
        (b0, b1, b2), (a1, a2) = _lp2(float(f1), float(rs.get("fixed_lp_q", 0.7)), fs)
        w = lfilter([b0, b1, b2], [1.0, a1, a2], w)
    gain = 10.0 ** (float(rs.get("gain_db", 0.0)) / 20.0)
    w = w * gain
    truth["Q"] = Q

    # ---- mix ------------------------------------------------------------
    lv = m.get("level", {"type": "linear", "in_max": 50.0, "out_max": 1.0})
    a_dir = float(apply_map(lv, DIR, fs))
    a_eff = float(apply_map(lv, EFF, fs))
    y = a_dir * mono + a_eff * w
    truth.update({"direct_gain": a_dir, "effect_gain": a_eff})

    # ---- the missing de-emphasis ---------------------------------------
    if b_.get("output_de_emphasis", "none") == "none" and spec.get("input_stage"):
        from .render import load_input_stage
        st = load_input_stage(spec, spec.get("_spec_path"))
        ba = _shelf_ba(fs, st) if st else None
        if ba is not None:
            y = lfilter(ba[0], ba[1], y)
            truth["output_de_emphasis"] = "none (pre-emphasis re-applied to cancel render.py's deferred filter)"

    return np.stack([y, y], axis=1), truth
