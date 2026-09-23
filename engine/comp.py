"""COMP — the AX30G's Block-1 compressor (mono).

Measured 2026-09-18 from the 11-row grid `capture/grids/comp.json`; the
derivation, every candidate law with its residual and what is still
under-determined are in `docs/comp-model-2026-09-18.md`.

The model in one line:

    y[n] = x[n] / (a + b * E[n])          E = peak envelope of |x|

i.e. the RECIPROCAL of the gain is affine in the detected envelope.  That is
what the 5 s ramp says: regressing 1/G on the linear amplitude of the 1 kHz
tone over -39..-0.5 dBFS leaves 0.08-0.25 % on the eight clean rows, and the
same two-parameter line reproduces the steady -20 and -40 dBFS sines.  There
is no threshold, no knee and no ratio: the output tends to the constant 1/b
(a limiter) and the gain tends to the constant 1/a (a sustainer), and every
setting is one point on that one-parameter family.

`a` is set by Sensitivity alone and `b` by both controls through

    b = B(Level) - a(Sensitivity) / Q,     Q = pivot_amplitude

which is the empirical finding that the five Sensitivity curves at Level 25
all cross at one point (input amplitude Q, gain 1/(B*Q)) -- the (a, b) pairs
lie on a straight line to 0.1 %.  Level 0 is a mute.

This module deliberately does NOT live in engine/effects.py: on 2026-09-18
another session was editing that file for the reverb C++ port.  Importing
`engine.comp` registers "COMP" in `engine.effects.RENDERERS` at run time, so
`engine.render.render_spec` dispatches to it exactly as if the entry were
written there.

The static dispatch entry WAS added to `engine/effects.py` later the same day,
once that session finished (`docs/rev-cpp-2-2026-09-18.md` appeared). The
run-time `setdefault` below is kept: it is a no-op when the static entry is
present, and it keeps `import engine.comp` sufficient on its own.
"""
import math
import numpy as np

from .params import apply_map


def _detector(x, aa, ar):
    """One-pole peak follower: rise coefficient aa, fall coefficient ar.

    E[n] = E[n-1] + (aa if |x[n]| > E[n-1] else ar) * (|x[n]| - E[n-1])

    A plain |x| rectifier with a fast attack and a slow release, which is what
    the captures say the detector is: a steady 1 kHz sine of amplitude A reads
    E = A to 0.4 % (so it rides the PEAK, not the mean or the rms), while
    -30 dBFS white noise reads 4.19 sigma, which no smoothed rectifier can do.
    """
    e = 0.0
    out = []
    ap = out.append
    for v in np.abs(x).tolist():
        e += (aa if v > e else ar) * (v - e)
        ap(e)
    return np.asarray(out)


def _level_B(mp, L, fs):
    """B(Level).  Two Levels were captured (25 and 50) plus the mute at 0, so
    the map is either those two points verbatim or the two-parameter law
    B = c / (Level + d) fitted through them, which is what the model ships:
    it is the only simple form that gets the measured ratio right
    (B(25)/B(50) = 1.9036, where a plain 1/Level would give 2.000, 0.34 dB
    out) AND stays finite everywhere.  It has NO support between 25 and 50 or
    below 25 -- capture/grids/comp-2.json's Level axis is what replaces it."""
    if mp.get("type") == "reciprocal_offset":
        return float(mp["c"]) / (float(L) + float(mp["d"]))
    return float(apply_map(mp, L, fs))


def _tau_to_coef(tau_ms, fs):
    if tau_ms is None or tau_ms <= 0:
        return 1.0
    return 1.0 - math.exp(-1.0 / (tau_ms * 1e-3 * fs))


def render_comp(x, spec, fs):
    """Block 1 Compressor. x is (N,2) at the device rate; the block is mono, so
    the detector runs on the mean of the two channels and the one gain
    multiplies both."""
    p = spec["params"]
    m = spec["maps"]
    b_ = spec.get("blocks", {})
    S = p["Sensitivity"]
    L = p["Level"]
    A = p["Attack"]

    truth = {"effect": "COMP", "Sensitivity": S, "Level": L, "Attack": A}

    if L <= 0:                                  # Level 0 is a mute (measured:
        truth["mute"] = True                    # both Level-0 captures sit at
        return np.zeros_like(x), truth          # -104 dBFS on the -20 sine)

    a = float(apply_map(m["a"], S, fs))
    B = _level_B(m["B"], L, fs)
    Q = float(b_.get("pivot_amplitude", 0.131198))
    if "b" in m:                 # a direct b (used by the fitting harness and
        b = float(apply_map(m["b"], L, fs))   # by any future per-row table)
    else:
        b = B - a / Q
    tau_a = float(apply_map(m["attack_tau_ms"], A, fs))
    rel = b_.get("detector", {})
    # two-stage option: a FIXED fast peak detector followed by a one-pole
    # smoother whose time constant is what the Attack control sets. When
    # detector.attack_tau_ms is absent the Attack control IS the detector's
    # own attack (one stage) -- both forms are tried in the model doc.
    tau_smooth = None
    if "attack_tau_ms" in rel:
        tau_smooth = tau_a
        tau_a = float(rel["attack_tau_ms"])
    tau_r = rel.get("release_tau_ms")
    if isinstance(tau_r, dict):
        tau_r = float(apply_map(tau_r, A, fs))
    tau_r = float(tau_r)

    aa = _tau_to_coef(tau_a, fs)
    ar = _tau_to_coef(tau_r, fs)

    det_in = x.mean(axis=1)
    nblk = int(rel.get("control_block", 1))
    if nblk > 1:
        # the gain computer runs once per block of `nblk` device samples, on
        # that block's own peak, and the gain it produces multiplies the whole
        # block -- so a transient inside a block is reduced by the step it
        # itself caused. That is the only structure tried that pulls a
        # single-sample click's PEAK down the way the unit does.
        n = len(det_in)
        pad = (-n) % nblk
        blk = np.abs(np.concatenate([det_in, np.zeros(pad)])).reshape(-1, nblk).max(axis=1)
        eb = _detector(blk, _tau_to_coef(tau_a, fs / nblk), _tau_to_coef(tau_r, fs / nblk))
        if tau_smooth:
            from scipy.signal import lfilter
            c = _tau_to_coef(tau_smooth, fs / nblk)
            eb = lfilter([c], [1.0, -(1.0 - c)], eb)
        e = np.repeat(eb, nblk)[:n]
    else:
        e = _detector(det_in, aa, ar)
        if tau_smooth:
            from scipy.signal import lfilter
            c = _tau_to_coef(tau_smooth, fs)
            e = lfilter([c], [1.0, -(1.0 - c)], e)
    if int(rel.get("gain_delay_samples", 0)):
        d = int(rel["gain_delay_samples"])
        e = np.concatenate([np.zeros(d), e[:-d]])

    floor = float(rel.get("envelope_floor", 0.0))
    if floor > 0.0:
        # a floor under the detector, i.e. a maximum gain of 1/(a + b*floor)
        # that scales with the setting. Off by default; see the model notes.
        e = np.maximum(e, floor)
    g = 1.0 / (a + b * e)
    gmax = b_.get("max_gain")
    if gmax:
        g = np.minimum(g, float(gmax))
    y = x * g[:, None]

    def _db(v):
        return 20.0 * math.log10(v) if v > 0 else float("-inf")
    truth.update({"a": a, "b": b, "B": B, "pivot_amplitude": Q,
                  "max_gain_db": -_db(a),
                  "ceiling_dbfs": -_db(b),
                  "threshold_dbfs": _db(a) - _db(b),
                  "attack_tau_ms": tau_a, "smooth_tau_ms": tau_smooth,
                  "release_tau_ms": tau_r,
                  "gain_min_db": _db(float(g.min())),
                  "gain_max_db": _db(float(g.max()))})
    return y, truth


# Run-time registration, kept alongside the static entry in engine/effects.py
# (see the module docstring): it makes `import engine.comp` sufficient on its
# own and is a no-op once effects.py has bound "COMP" itself.
try:
    from .effects import RENDERERS
    RENDERERS.setdefault("COMP", render_comp)
except Exception:                                    # pragma: no cover
    pass
