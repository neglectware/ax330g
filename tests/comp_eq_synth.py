#!/usr/bin/env python3
"""Synthetic recovery test for analysis/eq_response.py and
analysis/comp_curve.py.

Renders the normal signal set through Python stand-ins whose parameters are
known exactly -- a biquad EQ (low shelf + peaking mid + high shelf + gain)
and a feed-forward compressor (soft-knee static law, one-pole attack and
release on the gain in dB) -- wraps each render in a stand-in for the unit's
converter chain, writes them as 3-channel bench captures, and then runs the
two analysis scripts on them exactly as they would run on a real capture.
The point is the last column: recovered minus true.

    .venv/bin/python tests/comp_eq_synth.py            # both
    .venv/bin/python tests/comp_eq_synth.py --only eq
    .venv/bin/python tests/comp_eq_synth.py --keep     # leave the WAVs

What this does and does not prove
---------------------------------
It proves the measurement chain: the reference-channel alignment, the
capture-over-capture ratio that cancels the chain, the sweep deconvolution,
the noise and click cross-checks, the optimiser, the envelope detectors and
the exponential fits.  It does NOT prove the model *form* is the unit's --
the EQ truth is built from the same biquad family the fit assumes, and the
compressor truth from the same soft-knee law, because recovering known
parameters requires it.  The real unit's form is an open question that only
its own captures can answer, and the residual columns in
docs/3beq-response-<date>.md and docs/comp-curve-<date>.md are what will say
whether the form holds.

The synthetic converter chain here is a stand-in, not models/ax30g-chain.json:
three 5.49 Hz high-passes (the measured LF signature), a gentle HF shelf, a
0.27 ms delay and a level offset.  Both the effect capture and the bypass
reference get exactly the same one, so the analysis has something real to
cancel.
"""
import argparse
import json
import os
import sys

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfilt, hilbert, resample_poly, bilinear_zpk, zpk2sos

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)
from analysis.util import load_layout                              # noqa: E402
import analysis.eq_response as eqr                                  # noqa: E402
import analysis.comp_curve as cc                                    # noqa: E402

FS = 48000
DEVICE_RATE = 39062.5
UP, DOWN = 625, 768            # 48000 * 625 / 768 = 39062.5 exactly
OUTDIR = os.path.join(ROOT, "captures", "sim")
SIGNAL = os.path.join(ROOT, "capture", "signalset-normal.wav")
LAYOUT = os.path.join(ROOT, "capture", "layout-normal.json")

# --- ground truth -----------------------------------------------------------

EQ_TRUE = {"gain_db": -2.0,
           "low_fc_hz": 120.0, "low_q": 0.707, "low_gain_db": 8.0,
           "mid_fc_hz": 1200.0, "mid_q": 1.4, "mid_gain_db": -6.0,
           "high_fc_hz": 3200.0, "high_q": 0.707, "high_gain_db": 5.0}

COMP_TRUE = {"threshold_dbfs": -24.0, "ratio": 4.0, "knee_width_db": 8.0,
             "makeup_db": 6.0, "attack_ms": 12.0, "release_ms": 300.0}

NOISE_FLOOR_DBFS = -84.0        # the device's own floor, ahead of the compressor
REC_NOISE_DBFS = -104.0         # the interface's floor, after everything
SEND_REF_DB = -12.0             # level of the reference loop leg


# --- stand-ins --------------------------------------------------------------

def eq_sos(p, fs):
    """The truth EQ, as a cascade of RBJ biquads at `fs`."""
    parts = [eqr._shelf("low", fs, p["low_fc_hz"], p["low_q"], p["low_gain_db"]),
             eqr._peak(fs, p["mid_fc_hz"], p["mid_q"], p["mid_gain_db"]),
             eqr._shelf("high", fs, p["high_fc_hz"], p["high_q"], p["high_gain_db"])]
    return np.array([[b[0], b[1], b[2], a[0], a[1], a[2]] for b, a in parts])


def eq_block(x, fs, p=EQ_TRUE):
    return sosfilt(eq_sos(p, fs), x) * 10 ** (p["gain_db"] / 20.0)


def _static_gain_db(level_db, T, R, W):
    """Soft-knee gain reduction (<= 0 dB), written out here rather than
    imported, so a coding error in analysis/comp_curve.py's own soft_knee()
    cannot cancel itself out."""
    d = level_db - T
    over = np.zeros_like(d)
    if W > 0:
        k = np.abs(d) <= W / 2.0
        over[k] = (d[k] + W / 2.0) ** 2 / (2.0 * W)
        a = d > W / 2.0
        over[a] = d[a]
    else:
        a = d > 0
        over[a] = d[a]
    return -(1.0 - 1.0 / R) * over


def comp_block(x, fs, p=COMP_TRUE):
    """Feed-forward compressor.  The detector is the analytic envelope (an
    ideal level detector with no time constant of its own), so the gain
    trajectory is exactly a one-pole with the stated attack / release and the
    recovered tau has nothing else folded into it."""
    env = np.abs(hilbert(x))
    lvl = 20.0 * np.log10(np.maximum(env, 1e-12))
    target = _static_gain_db(lvl, p["threshold_dbfs"], p["ratio"], p["knee_width_db"])
    ca = 1.0 - np.exp(-1.0 / (fs * p["attack_ms"] / 1000.0))
    cr = 1.0 - np.exp(-1.0 / (fs * p["release_ms"] / 1000.0))
    g = np.empty_like(target)
    gi = 0.0
    for i in range(len(target)):
        t = target[i]
        gi += (t - gi) * (ca if t < gi else cr)
        g[i] = gi
    return x * 10 ** ((g + p["makeup_db"]) / 20.0)


def device(x, block):
    """Resample 48 kHz -> 39062.5 Hz, run the block, resample back.  The
    bypass render uses block=None and goes through the identical resampler
    pair, so the resamplers cancel in the capture-over-capture ratio."""
    d = resample_poly(x, UP, DOWN)
    if block is not None:
        d = block(d, DEVICE_RATE)
    return resample_poly(d, DOWN, UP)[: len(x)]


def chain(y, fs=FS):
    """Stand-in for the unit's converter chain: three 5.49 Hz high-passes,
    a gentle high shelf, a 0.27 ms delay, a level offset."""
    sos = []
    for _ in range(3):
        z, p_, k = bilinear_zpk([0.0], [-2 * np.pi * 5.49], 1.0, fs)
        sos.append(zpk2sos(z, p_, k)[0])
    y = sosfilt(np.array(sos), y)
    b, a = eqr._shelf("high", fs, 9000.0, 0.7, -3.0)
    y = sosfilt(np.array([[b[0], b[1], b[2], a[0], a[1], a[2]]]), y)
    n = int(round(0.00027 * fs))
    return np.concatenate([np.zeros(n), y])[: len(y)] * 10 ** (-1.5 / 20.0)


def render_capture(x, block, path, seed):
    """One synthetic bench capture: 3 channels (AX30G L, AX30G R, reference
    loop), a random lead-in, the device's own noise floor ahead of the block
    and the interface's after the chain."""
    rng = np.random.default_rng(seed)
    xin = x + rng.standard_normal(len(x)) * 10 ** (NOISE_FLOOR_DBFS / 20.0)
    y = chain(device(xin, block))
    lead = int(rng.uniform(0.5, 4.0) * FS)
    n = lead + len(y) + int(3 * FS)
    out = np.zeros((n, 3))
    out[lead:lead + len(y), 0] = y
    out[lead:lead + len(y), 1] = y
    out[lead:lead + len(x), 2] = x * 10 ** (SEND_REF_DB / 20.0)
    out += rng.standard_normal(out.shape) * 10 ** (REC_NOISE_DBFS / 20.0)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sf.write(path, out.astype(np.float32), FS, subtype="FLOAT")
    return path


# --- comparison -------------------------------------------------------------

def row(rows, name, got, true, unit="", tol=None):
    err = None if (got is None or true is None) else got - true
    rows.append((name, got, true, err, unit, tol))
    return err


def print_table(title, rows):
    print()
    print(title)
    print(f"  {'quantity':26s} {'recovered':>12s} {'true':>12s} {'error':>12s}  {'':4s} {'':s}")
    ok = True
    for name, got, true, err, unit, tol in rows:
        g = "—" if got is None else f"{got:12.4f}"
        t = "—" if true is None else f"{true:12.4f}"
        e = "—" if err is None else f"{err:+12.4f}"
        flag = ""
        if tol is not None:
            good = err is not None and abs(err) <= tol
            ok = ok and good
            flag = "ok " if good else "OUT"
        print(f"  {name:26s} {g} {t} {e}  {unit:4s} {flag}")
    return ok


def run_eq(keep, outdir):
    x, fs = sf.read(SIGNAL, dtype="float64", always_2d=True)
    x = x[:, 0]
    cap = render_capture(x, eq_block, os.path.join(OUTDIR, "SYNTH_3BEQ_IN-LIN.wav"), 11)
    ref = render_capture(x, None, os.path.join(OUTDIR, "SYNTH_BYPASS_IN-LIN.wav"), 12)
    r = eqr.analyze_one(cap, ref, SIGNAL, LAYOUT, outdir=outdir)
    p = r["fit"]
    rows = []
    row(rows, "broadband gain", p["gain_db"], EQ_TRUE["gain_db"], "dB", 0.25)
    row(rows, "low shelf fc", p["low_fc_hz"], EQ_TRUE["low_fc_hz"], "Hz", 12.0)
    row(rows, "low shelf Q", p["low_q"], EQ_TRUE["low_q"], "", 0.10)
    row(rows, "low shelf gain", p["low_gain_db"], EQ_TRUE["low_gain_db"], "dB", 0.25)
    row(rows, "mid peak fc", p["mid_fc_hz"], EQ_TRUE["mid_fc_hz"], "Hz", 60.0)
    row(rows, "mid peak Q", p["mid_q"], EQ_TRUE["mid_q"], "", 0.15)
    row(rows, "mid peak gain", p["mid_gain_db"], EQ_TRUE["mid_gain_db"], "dB", 0.25)
    row(rows, "high shelf fc", p["high_fc_hz"], EQ_TRUE["high_fc_hz"], "Hz", 200.0)
    row(rows, "high shelf Q", p["high_q"], EQ_TRUE["high_q"], "", 0.15)
    row(rows, "high shelf gain", p["high_gain_db"], EQ_TRUE["high_gain_db"], "dB", 0.25)
    ok = print_table("EQ — analysis/eq_response.py against a known biquad cascade", rows)
    cx = r["cross_check"]
    print(f"  fit residual over {r['fit_band_hz'][0]:g}-{r['fit_band_hz'][1]:g} Hz: "
          f"{r['fit_residual_rms_db']:.4f} dB rms, {r['fit_residual_max_db']:.4f} dB max")
    print(f"  cross-check: noise−sweep {cx['noise_vs_sweep_rms_db']:.3f} dB rms "
          f"({cx['noise_vs_sweep_max_db']:.3f} max); clicks−sweep "
          f"{cx['clicks_vs_sweep_rms_db']:.3f} dB rms ({cx['clicks_vs_sweep_max_db']:.3f} max)")
    print(f"  phase residual vs the minimum-phase fit: "
          f"{r['phase_residual_rms_deg_100_10k']:.2f} deg rms, 100 Hz-10 kHz")
    print(f"  wrote {os.path.relpath(r['csv'], ROOT)}" + (f" and {os.path.relpath(r['png'], ROOT)}" if r["png"] else ""))
    if not keep:
        for p_ in (cap, ref):
            os.remove(p_)
    return ok, r


def run_comp(keep, outdir):
    x, fs = sf.read(SIGNAL, dtype="float64", always_2d=True)
    x = x[:, 0]
    cap = render_capture(x, comp_block, os.path.join(OUTDIR, "SYNTH_COMP_IN-LIN.wav"), 21)
    ref = render_capture(x, None, os.path.join(OUTDIR, "SYNTH_BYPASS_IN-LIN.wav"), 12)
    r = cc.analyze_one(cap, ref, SIGNAL, LAYOUT, outdir=outdir)
    s = r["static"]
    a = r["attack"]
    rn = r["release_noise_floor"]
    rows = []
    row(rows, "threshold", s["threshold_dbfs"], COMP_TRUE["threshold_dbfs"], "dBFS", 1.0)
    row(rows, "ratio", s["ratio"], COMP_TRUE["ratio"], ":1", 0.4)
    row(rows, "knee width", s["knee_width_db"], COMP_TRUE["knee_width_db"], "dB", 2.5)
    row(rows, "makeup", s["makeup_db"], COMP_TRUE["makeup_db"], "dB", 0.3)
    row(rows, "attack tau", a["attack_tau_ms_median"], COMP_TRUE["attack_ms"], "ms", 3.0)
    row(rows, "release tau (floor)", rn["release_tau_ms_median"], COMP_TRUE["release_ms"], "ms", 90.0)
    # the two steady-state gains follow from the static law, so they are truths too
    for seg_name, lvl in (("sine20", -20.0), ("sine40", -40.0)):
        t = float(_static_gain_db(np.array([lvl]), COMP_TRUE["threshold_dbfs"],
                                  COMP_TRUE["ratio"], COMP_TRUE["knee_width_db"])[0]) + COMP_TRUE["makeup_db"]
        row(rows, f"steady gain at {lvl:.0f} dBFS", r["steady"][seg_name + "_gain_db"], t, "dB", 0.4)
    ok = print_table("COMP — analysis/comp_curve.py against a known feed-forward compressor", rows)
    print(f"  static fit residual over {s['fit_band_dbfs'][0]:g}..{s['fit_band_dbfs'][1]:g} dBFS: "
          f"{s['residual_rms_db']:.4f} dB rms, {s['residual_max_db']:.4f} dB max, n={s['n_points']}")
    print(f"  max gain reduction on the ramp: {s['max_gain_reduction_db']:.3f} dB")
    print(f"  attack spread over the four bursts: {a['attack_tau_ms_spread']:.3f} ms")
    print(f"  release (floor) spread {rn['release_tau_ms_spread']:.1f} ms over n={rn['n']}; "
          f"gain excursion {rn['excursion_db']:.3f} dB over fit residual {rn['residual_rms_db']:.3f} dB "
          f"= {rn['excursion_over_residual']:.1f}x; gap floor {rn['floor_dbfs']:.1f} dBFS")
    print(f"  release (burst onsets) spread {r['release_burst_onsets'].get('spread_db', float('nan')):.3f} dB")
    print(f"  release (-40 dBFS onset minus steady) {r['release_sine40']['still_recovering_db']:+.3f} dB")
    print(f"  click peak gain median {r['clicks']['median_db']:+.3f} dB")
    print(f"  wrote {os.path.relpath(r['csv'], ROOT)}" + (f" and {os.path.relpath(r['png'], ROOT)}" if r["png"] else ""))
    if not keep:
        for p_ in (cap, ref):
            if os.path.exists(p_):
                os.remove(p_)
    return ok, r


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", choices=["eq", "comp"])
    ap.add_argument("--keep", action="store_true", help="leave the synthetic captures on disk")
    ap.add_argument("--outdir", default=os.path.join(ROOT, "out", "synth"))
    ap.add_argument("--json", help="write the two result dicts here")
    a = ap.parse_args()
    if not os.path.exists(SIGNAL):
        raise SystemExit(f"{SIGNAL} missing — run `make signals-normal` first")
    res = {}
    ok = True
    if a.only in (None, "eq"):
        o, res["eq"] = run_eq(a.keep, a.outdir)
        ok = ok and o
    if a.only in (None, "comp"):
        o, res["comp"] = run_comp(a.keep, a.outdir)
        ok = ok and o
    print()
    print("ALL WITHIN TOLERANCE" if ok else "SOMETHING IS OUT OF TOLERANCE — see the OUT rows above")
    if a.json:
        with open(a.json, "w") as fh:
            json.dump(res, fh, indent=1, default=float)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
