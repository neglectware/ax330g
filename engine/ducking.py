"""Ducking for the four Ambience-slot delays (SDLY, XDLY, TDLY, HDLY).

Measured 2026-09-18 from the seven `capture/grids/sdly-ducking.json` captures;
the evidence is in `docs/ducking-model-2026-09-18.md`. What the unit does:

  * the ducker is a **gain on the wet output only** -- the delay line, its
    feedback and the dry path are untouched. Evidence: at Feedback 40 the
    tone's tail outlasts the tone, and from 300 ms after the tone stops the
    Ducking-50 tail is level-for-level the Ducking-0 tail (a write-side or
    feedback-side ducker would leave the stored signal permanently quieter);
    the loop coefficient read off the repeat peaks is 0.483 at every Ducking
    value; and the release trajectory is identical at Feedback 25 and 40.
  * the control signal is a **one-pole-smoothed full-wave rectifier on the
    effect's INPUT** (not its output: same trajectory at both feedbacks).
  * the law is **subtractive and linear in both Ducking and the envelope**:

        g[n] = clip(1 - k * Ducking * env[n],  min_gain, 1)

    with no threshold and no knee -- (1-g)/Ducking collapses onto one curve
    for Ducking 10/25/40/50 over the whole -36..-8 dBFS ramp.

This module deliberately does not touch `engine/effects.py`: it calls the
existing `render_sdly` read-only for the wet path (the renderer is linear in
the dry/wet mix, so forcing `maps.dry` to zero isolates it exactly) and adds
the dry path itself.
"""
import copy
import json
import math
import os

import numpy as np

from .effects import render_sdly
from .params import apply_map

DUCKED_EFFECTS = ("SDLY", "XDLY", "TDLY", "HDLY")
DEFAULT_DUCKING = "ax30g-ducking.json"


def load_ducking(spec, spec_path=None):
    """spec["ducking"]: inline dict, or a file name relative to the spec's
    directory (models/) -- see models/ax30g-ducking.json. Returns the dict or
    None. A dict carrying a "file" key is that file's contents with the dict's
    own other keys written over them (same convention as load_input_stage)."""
    c = spec.get("ducking")
    if c is None:
        return None

    def _read(name):
        base = os.path.dirname(spec_path) if spec_path else os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "..", "models")
        with open(os.path.join(base, name)) as f:
            return json.load(f)

    if isinstance(c, str):
        return _read(c)
    if isinstance(c, dict) and "file" in c:
        d = _read(c["file"])
        d.update({k: v for k, v in c.items() if k != "file"})
        return d
    return c


def detector_envelope(x, fs, duck):
    """The ducker's control signal at the device rate.

    x: (N,) or (N,2) device-rate input to the effect. The AX30G has one
    (mono) Guitar In, so the detector is mono: a stereo array is summed the
    way render_modd/render_smod sum it.

    Returns env (N,), the smoothed rectified input amplitude.
    """
    from scipy.signal import lfilter
    d = duck.get("detector", {})
    if np.ndim(x) == 2:
        e = 0.5 * (x[:, 0] + x[:, 1])
    else:
        e = np.asarray(x, dtype=float)
    pre = d.get("prefilter")
    if pre:
        # the detector's frequency weighting, measured from the sweep
        # (docs/ducking-model-2026-09-18.md section 6a): unity at 1 kHz,
        # -6.10 dB at 40 Hz, +0.78 dB at 12 kHz -- the measured weighting with
        # the peak stage's own 2.2 dB taken out. Coefficients are at the
        # device rate; at 39062.5 vs the 39063.83 they were fitted at, the
        # corners move by 34 ppm.
        e = lfilter(np.asarray(pre["b"], dtype=float),
                    np.asarray(pre["a"], dtype=float), e)
    rect = d.get("rectifier", "abs")
    if rect == "abs":
        e = np.abs(e)
    elif rect == "square":
        e = e * e
    else:
        raise ValueError("unknown rectifier %r" % rect)

    pk = d.get("peak")
    if pk:
        # optional fast stage ahead of the slow smoother: a peak follower with
        # a near-instant attack and a short release. It leaves a steady sine
        # alone (k is calibrated there) but raises the envelope of peaky
        # material, which is what the noise and DI segments need -- see
        # docs/ducking-model-2026-09-18.md section 7.
        e = _follow(e, float(pk.get("attack_ms", 0.02)) * 1e-3,
                    float(pk.get("release_ms", 1.0)) * 1e-3, fs)

    ta = float(d.get("attack_ms", 52.0)) * 1e-3
    tr = float(d.get("release_ms", 52.0)) * 1e-3
    env = _follow(e, ta, tr, fs)
    if rect == "square":
        env = np.sqrt(np.maximum(env, 0.0))
    return env


def _follow(e, ta, tr, fs):
    """One-pole follower with separate attack and release time constants (s)."""
    from scipy.signal import lfilter
    aa = math.exp(-1.0 / max(ta * fs, 1e-9))
    ar = math.exp(-1.0 / max(tr * fs, 1e-9))
    if abs(aa - ar) < 1e-15:
        return lfilter([1.0 - aa], [1.0, -aa], e)
    out = np.empty_like(e)
    z = 0.0
    for i in range(len(e)):
        a = aa if e[i] > z else ar
        z = a * z + (1.0 - a) * e[i]
        out[i] = z
    return out


def ducking_gain(env, ducking_value, duck):
    """g[n] from the envelope. Subtractive law, clipped."""
    gm = duck.get("gain", {})
    law = gm.get("type", "linear_subtract")
    k = float(gm["k"])
    dmap = duck.get("maps", {}).get("ducking")
    amount = apply_map(dmap, ducking_value, 1.0) if dmap else float(ducking_value)
    if law != "linear_subtract":
        raise ValueError("unknown ducking law %r" % law)
    g = 1.0 - k * amount * env
    return np.clip(g, float(gm.get("min_gain", 0.0)), float(gm.get("max_gain", 1.0)))


def _dry_wet_specs(spec):
    """A copy of the SDLY spec whose dry path is silenced, so render_sdly
    returns the wet path alone (the renderer's output is
    dry*x + wet*read, linear in both)."""
    wet = copy.deepcopy(spec)
    wet["maps"] = dict(wet["maps"])
    wet["maps"]["dry"] = {"type": "constant", "value": 0.0}
    return wet


def render_sdly_ducking(x, spec, fs):
    """Stereo Delay with the Ducking parameter. Same signature as the
    renderers in engine/effects.py; `spec["ducking"]` must already be the
    resolved dict (use load_ducking) and `spec["params"]["Ducking"]` the panel
    value. With Ducking 0, or no ducking block, this is bit-for-bit
    render_sdly."""
    duck = spec.get("ducking")
    p = spec["params"]
    dv = float(p.get("Ducking", 0.0))
    if not duck or dv == 0.0:
        return render_sdly(x, spec, fs)

    wet_out, truth = render_sdly(x, _dry_wet_specs(spec), fs)
    m = spec["maps"]
    env = detector_envelope(x, fs, duck)
    g = ducking_gain(env, dv, duck)
    y = np.empty_like(wet_out)
    for ch, side in enumerate(("L", "R")):
        dry = apply_map(m["dry"], p[f"{side} Bal"], fs) if "dry" in m else \
            1.0 - apply_map(m["balance"], p[f"{side} Bal"], fs)
        y[:, ch] = dry * x[:, ch] + g * wet_out[:, ch]
        truth["channels"][ch]["dry"] = dry
    truth["ducking"] = {
        "value": dv, "gain_min": float(np.min(g)), "gain_mean": float(np.mean(g)),
        "env_max": float(np.max(env)),
    }
    return y, truth


# ---------------------------------------------------------------------------
# render_spec with the ducker, mirroring engine.render.render_spec.
#
# Folded into engine/render.py's render_spec on 2026-09-18 (and
# models/ax30g-sdly.json gained its "ducking" key), so the ordinary
# render_spec / tests/null_test.py path now handles a Ducking value on its
# own. This wrapper is kept as the standalone mirror the measurement was done
# through, and is what tests/ducking_null.py still calls.
# ---------------------------------------------------------------------------

def render_spec_ducking(spec, x, fs_in, spec_path=None):
    """x: (N,) or (N,2) at fs_in. Returns (y (N,2) at fs_in, truth)."""
    from fractions import Fraction
    from scipy.signal import resample_poly
    from .render import (load_chain, apply_chain, load_input_stage,
                         apply_input_stage, resampler_window, _converter)

    fs = float(spec.get("sample_rate", 39062.5))
    if x.ndim == 1:
        x = np.stack([x, x], axis=1)
    fr = Fraction(fs / fs_in).limit_denominator(4096)
    up, down = fr.numerator, fr.denominator
    win = resampler_window(spec, up, down)
    bits = spec.get("converters", {}).get("bits", 18)
    stage = load_input_stage(spec, spec_path)
    stage_info = {}
    if stage and stage.get("rate") == "input":
        x, stage_info = apply_input_stage(x, fs_in, stage)
        xd = resample_poly(x, up, down, axis=0, window=win)
        xd = _converter(xd, bits)
    else:
        xd = resample_poly(x, up, down, axis=0, window=win)
        xd, stage_info = apply_input_stage(xd, fs, stage, bits)
        if not stage_info:
            xd = _converter(xd, bits)
    spec = dict(spec)
    # models/ax30g-sdly.json does not carry a "ducking" key yet (see the TODO
    # above), so fall back to the measured block when the caller has not
    # supplied one and the panel value is non-zero.
    if spec.get("ducking") is None and float(spec["params"].get("Ducking", 0)):
        spec["ducking"] = DEFAULT_DUCKING
    spec["ducking"] = load_ducking(spec, spec_path)
    y, truth = render_sdly_ducking(xd, spec, fs)
    y = _converter(y, bits)
    yo = resample_poly(y, down, up, axis=0, window=win)
    yo = yo[: x.shape[0]]
    yo = apply_chain(yo, fs_in, load_chain(spec, spec_path))
    truth["sample_rate"] = fs
    if stage_info:
        truth["input_stage"] = stage_info
    return yo, truth
