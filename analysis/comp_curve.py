#!/usr/bin/env python3
"""COMP: measure the compressor from a capture and fit a static curve and
its time constants.

    comp_curve.py CAPTURE.wav [--ref BYPASS_OR_ALLZERO.wav]
    comp_curve.py --grid capture/grids/comp.json [--out docs/comp-curve-<date>.md]

Method
------
Like analysis/eq_response.py, every number is a RATIO of two captures and
never a ratio against the stimulus, so the converter chain and the input
stage (both already measured: models/ax30g-chain.json,
docs/input-stage-2026-09-16.md) cancel instead of being re-fitted here.

  reference   captures/AX30G_BYPASS_IN-LIN.wav by default, or the grid's
              all-zero row.  Same signal set, same rig, no compressor.
  send level  the two recordings can differ slightly in send level; the
              difference is measured on the two reference channels over the
              1 kHz sine and applied, so the x axis is the stimulus level in
              the signal set's own dBFS scale.

What each segment gives:

  ramp     1 kHz, -40 -> 0 dBFS over 5 s = 8 dB/s.  Slow enough that the
           detector is in steady state at every point, so the output/input
           envelope pair IS the static law.  Fitted with a soft-knee
           threshold/ratio/knee/makeup model.
  bursts   four 60 ms 1 kHz bursts at -20 dBFS.  ATTACK is the exponential
           the gain follows inside a burst.  RELEASE has no signal to ride
           on once a burst stops, so it is estimated three ways, each
           reported with what limits it:
             (a) the device's own noise floor in the gap, which the output
                 gain still multiplies;
             (b) the gain at the onset of bursts 2/3/4 versus burst 1
                 (resolves a release of the order of the 2.0/2.3/2.7 s
                 spacings, nothing faster);
             (c) the first 100 ms of the -40 dBFS sine versus its last
                 500 ms, 1.5 s after the -20 dBFS sine stops.
  sine20   steady gain at -20 dBFS.
  sine40   steady gain at -40 dBFS.  With Level as makeup, this is the row
           that reads makeup directly if -40 dBFS is below threshold.
  clicks   peak-vs-rms detector probe: a -16 dBFS single-sample click has
           almost no rms energy.
  di       held out; never fitted.
"""
import argparse
import datetime
import importlib.util
import json
import os
import re
import sys

import numpy as np
from scipy.optimize import least_squares
from scipy.signal import butter, sosfiltfilt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)
from analysis.util import load_wav, load_layout, seg, cut, db      # noqa: E402
from analysis.run import load_aligned                              # noqa: E402

DEFAULT_SIGNAL = os.path.join(ROOT, "capture", "signalset-normal.wav")
DEFAULT_LAYOUT = os.path.join(ROOT, "capture", "layout-normal.json")
DEFAULT_BYPASS = os.path.join(ROOT, "captures", "AX30G_BYPASS_IN-LIN.wav")
CAPTURE_DIR = os.path.join(ROOT, "captures")

PARAM_RE = re.compile(r"([A-Za-z]+)-(-?\d+(?:\.\d+)?)")
LONG_NAMES = {"Sensitivity": "Sensitivity", "Level": "Level", "Attack": "Attack"}


def _capture_fname():
    """capture/capture.py's own fname(), imported rather than copied."""
    p = os.path.join(ROOT, "capture", "capture.py")
    spec = importlib.util.spec_from_file_location("_ax_capture_c", p)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m.fname


def params_from_name(name):
    out = {}
    body = os.path.basename(name).split("_IN-")[0]
    for k, v in PARAM_RE.findall(body):
        if k in LONG_NAMES:
            out[LONG_NAMES[k]] = float(v) if "." in v else int(v)
    return out


# --------------------------------------------------------------- envelopes

def tone_envelope(x, fs, hz=1000.0, lp_hz=60.0, order=4):
    """Amplitude envelope of a tone at `hz` by complex demodulation and a
    zero-phase low-pass.  Zero phase matters: a causal detector would bias
    every attack time constant by its own group delay."""
    t = np.arange(len(x)) / fs
    z = x * np.exp(-2j * np.pi * hz * t)
    sos = butter(order, lp_hz, "low", fs=fs, output="sos")
    zr = sosfiltfilt(sos, z.real)
    zi = sosfiltfilt(sos, z.imag)
    return 2.0 * np.hypot(zr, zi)


def rms_windows(x, fs, t0, t1, win_ms):
    n = int(round(win_ms * fs / 1000.0))
    a = int(round(t0 * fs))
    b = int(round(t1 * fs))
    ts, vs = [], []
    for i in range(a, b - n, n):
        ts.append((i + n / 2) / fs)
        vs.append(float(np.sqrt(np.mean(x[i:i + n] ** 2))))
    return np.array(ts), np.array(vs)


def send_offset_db(ainfo_cap, ainfo_ref):
    """dB the reference capture has to be raised by to sit at the capture's
    own send level."""
    a = ainfo_cap.get("ref_gain_db")
    b = ainfo_ref.get("ref_gain_db")
    if a is None or b is None:
        return 0.0
    return float(a - b)


# ------------------------------------------------------------ static curve

def static_curve(cap, ref, fs, layout, ainfo_cap, ainfo_ref,
                 lp_hz=50.0, step_ms=10.0, edge_s=0.08):
    """Input/output curve from the ramp.

    x = stimulus level in the signal set's own dBFS scale (the capture's
    reference channel, de-scaled by the send gain the aligner measured).
    y = the capture's output level.
    chain = the same stimulus through the reference capture, raised to this
    capture's send level; gain = y - chain is the compressor's own gain.
    """
    s = seg(layout, "ramp")
    t0, t1 = s["start"] + edge_s, s["end"] - edge_s
    off = send_offset_db(ainfo_cap, ainfo_ref)
    sg = ainfo_cap.get("ref_gain_db") or 0.0

    ec = tone_envelope(cut(cap[:, 0], fs, t0, t1), fs, s.get("hz", 1000.0), lp_hz)
    er = tone_envelope(cut(ref[:, 0], fs, t0, t1), fs, s.get("hz", 1000.0), lp_hz)
    ex = tone_envelope(cut(cap[:, 2], fs, t0, t1), fs, s.get("hz", 1000.0), lp_hz)
    n = min(len(ec), len(er), len(ex))
    step = int(round(step_ms * fs / 1000.0))
    idx = np.arange(0, n, step)

    x_dbfs = db(ex[idx]) - sg
    y_db = db(ec[idx])
    chain_db = db(er[idx]) + off
    return {"x_dbfs": x_dbfs, "out_db": y_db, "chain_db": chain_db,
            "gain_db": y_db - chain_db,
            "t_s": (idx / fs) + t0}


def soft_knee(x, T, R, W, M):
    """Output level for input level x (both dB).  Standard soft knee:
    below T-W/2 the gain is M, above T+W/2 the slope is 1/R, quadratic
    between."""
    d = x - T
    over = np.zeros_like(d)
    knee = np.abs(d) <= W / 2.0 if W > 1e-6 else np.zeros_like(d, dtype=bool)
    above = d > W / 2.0 if W > 1e-6 else d > 0
    if W > 1e-6:
        over[knee] = (d[knee] + W / 2.0) ** 2 / (2.0 * W)
    over[above] = d[above]
    return x + M - (1.0 - 1.0 / R) * over


def fit_static(curve, fit_lo=-38.0, fit_hi=-1.0):
    """Fit T/R/W/M to the ramp curve over [fit_lo, fit_hi] dBFS.  The top of
    the ramp is excluded by default: the stimulus reaches 0 dBFS and the ADC
    hard clip (docs/input-stage-2026-09-16.md) is only approximately
    cancelled by dividing two captures there."""
    x = curve["x_dbfs"]
    y = curve["out_db"] - curve["chain_db"] + curve["x_dbfs"]   # output in the input's own scale
    m = (x >= fit_lo) & (x <= fit_hi) & np.isfinite(x) & np.isfinite(y)
    xx, yy = x[m], y[m]
    if len(xx) < 10:
        return None

    def resid(p):
        T, R, W, M = p
        return soft_knee(xx, T, R, W, M) - yy

    best = None
    for T0 in (-35.0, -28.0, -20.0, -12.0, -6.0):
        for R0 in (1.5, 3.0, 8.0):
            p0 = [T0, R0, 6.0, float(np.median(yy[xx < fit_lo + 6] - xx[xx < fit_lo + 6])) if (xx < fit_lo + 6).any() else 0.0]
            try:
                r = least_squares(resid, p0, bounds=([-60.0, 1.0, 0.0, -30.0],
                                                     [6.0, 60.0, 40.0, 40.0]), max_nfev=3000)
            except Exception:
                continue
            rms = float(np.sqrt(np.mean(r.fun ** 2)))
            if best is None or rms < best[1]:
                best = (r.x, rms)
    if best is None:
        return None
    p, rms = best
    T, R, W, M = (float(v) for v in p)
    gr = curve["gain_db"][m] - float(np.median(curve["gain_db"][m][xx < fit_lo + 6])) if (xx < fit_lo + 6).any() else curve["gain_db"][m]
    return {"threshold_dbfs": T, "ratio": R, "knee_width_db": W, "makeup_db": M,
            "residual_rms_db": rms,
            "residual_max_db": float(np.max(np.abs(resid(p)))),
            "fit_band_dbfs": [fit_lo, fit_hi],
            "max_gain_reduction_db": float(-np.min(gr)),
            "n_points": int(len(xx))}


# ------------------------------------------------------------ time constants

def _fit_exp(t, g):
    """g(t) = g_inf + (g0 - g_inf) exp(-t/tau).  Returns (tau_ms, g0, g_inf,
    rms residual dB)."""
    if len(t) < 6:
        return None

    def resid(p):
        tau, g0, gi = p
        return gi + (g0 - gi) * np.exp(-t / max(tau, 1e-5)) - g

    span = max(t[-1] - t[0], 1e-4)
    best = None
    for tau0 in (span / 20, span / 5, span, span * 3):
        try:
            r = least_squares(resid, [tau0, g[0], g[-1]],
                              bounds=([1e-5, -60.0, -60.0], [span * 50, 60.0, 60.0]),
                              max_nfev=2000)
        except Exception:
            continue
        rms = float(np.sqrt(np.mean(r.fun ** 2)))
        if best is None or rms < best[1]:
            best = (r.x, rms)
    if best is None:
        return None
    p, rms = best
    return {"tau_ms": float(p[0]) * 1000.0, "g0_db": float(p[1]), "g_inf_db": float(p[2]),
            "residual_rms_db": rms}


def attack_from_bursts(cap, ref, fs, layout, ainfo_cap, ainfo_ref,
                       lp_hz=400.0, skip_ms=4.0, use_ms=52.0):
    """Gain trajectory inside each 60 ms burst.  The first `skip_ms` are
    dropped (the burst has a 3 ms raised-cosine fade-in, and the envelope
    detector's own zero-phase smear is about 1 ms at lp_hz=400)."""
    s = seg(layout, "bursts")
    off = send_offset_db(ainfo_cap, ainfo_ref)
    out = []
    for k, tb in enumerate(s["burst_times"]):
        a, b = tb - 0.02, tb + 0.12
        ec = tone_envelope(cut(cap[:, 0], fs, a, b), fs, s.get("hz", 1000.0), lp_hz)
        er = tone_envelope(cut(ref[:, 0], fs, a, b), fs, s.get("hz", 1000.0), lp_hz)
        n = min(len(ec), len(er))
        t = np.arange(n) / fs - 0.02
        g = db(ec[:n]) - db(er[:n]) - off
        m = (t >= skip_ms / 1000.0) & (t <= use_ms / 1000.0) & (db(er[:n]) > db(er[:n]).max() - 25)
        fit = _fit_exp(t[m] - t[m][0], g[m]) if m.sum() >= 6 else None
        out.append({"burst": k, "t_s": float(tb),
                    "gain_start_db": float(np.median(g[m][:3])) if m.sum() >= 3 else None,
                    "gain_end_db": float(np.median(g[m][-3:])) if m.sum() >= 3 else None,
                    "fit": fit,
                    "_t": t[m] if m.sum() else np.array([]),
                    "_g": g[m] if m.sum() else np.array([])})
    taus = [b["fit"]["tau_ms"] for b in out if b["fit"]]
    depth = [b["fit"]["g0_db"] - b["fit"]["g_inf_db"] for b in out if b["fit"]]
    return {"per_burst": out,
            "attack_tau_ms_median": float(np.median(taus)) if taus else None,
            "attack_tau_ms_spread": float(np.std(taus)) if taus else None,
            "attack_depth_db_median": float(np.median(depth)) if depth else None}


def release_from_noise_floor(cap, ref, fs, layout, ainfo_cap, ainfo_ref,
                             win_ms=25.0, span_s=1.6):
    """(a) The device's own noise floor in the gap after each burst is still
    multiplied by the output gain, so its level traces the release.

    The fit is only worth reading when the gain actually moves more than the
    fit scatter: `excursion_db` (the fitted g0 - g_inf) against
    `residual_rms_db` is that test, and it is reported instead of a bare
    time constant.  `floor_dbfs` is the level the measurement rode on, for
    context -- a gap floor down near the interface's own noise will show up
    as a large residual.
    """
    s = seg(layout, "bursts")
    sil = seg(layout, "silence")
    off = send_offset_db(ainfo_cap, ainfo_ref)
    _, q = rms_windows(ref[:, 0], fs, sil["start"] + 0.2, sil["end"] - 0.2, win_ms)
    floor_sil = db(np.median(q)) if len(q) else None

    fits, floors = [], []
    times = list(s["burst_times"])
    for k, tb in enumerate(times):
        t_end = tb + s.get("len_s", 0.06)
        t_stop = min(t_end + span_s, times[k + 1] - 0.05 if k + 1 < len(times) else t_end + span_s)
        if t_stop - t_end < 0.2:
            continue
        tc, vc = rms_windows(cap[:, 0], fs, t_end + 0.03, t_stop, win_ms)
        tr, vr = rms_windows(ref[:, 0], fs, t_end + 0.03, t_stop, win_ms)
        n = min(len(vc), len(vr))
        if n < 6:
            continue
        floors.append(float(db(np.median(vr[:n]))))
        g = db(vc[:n]) - db(vr[:n]) - off
        fits.append(_fit_exp(tc[:n] - tc[0], g))
    fits = [f for f in fits if f]
    taus = [f["tau_ms"] for f in fits]
    exc = [abs(f["g0_db"] - f["g_inf_db"]) for f in fits]
    res = [f["residual_rms_db"] for f in fits]
    return {"method": "noise floor in the gap",
            "release_tau_ms_median": float(np.median(taus)) if taus else None,
            "release_tau_ms_spread": float(np.std(taus)) if taus else None,
            "excursion_db": float(np.median(exc)) if exc else None,
            "residual_rms_db": float(np.median(res)) if res else None,
            "excursion_over_residual": (float(np.median(exc) / np.median(res))
                                        if res and np.median(res) > 0 else None),
            "floor_dbfs": float(np.median(floors)) if floors else None,
            "silence_floor_dbfs": floor_sil,
            "n": len(taus)}


def release_from_burst_onsets(attack):
    """(b) If the release outlasts the gap, burst j starts at a lower gain
    than burst 1.  Spacings are 2.0 / 2.3 / 2.7 s, so this only resolves a
    release of that order; it is reported as the recovered gain, not as a
    time constant, unless the four points actually fall on an exponential."""
    pts = [(b["t_s"], b["gain_start_db"]) for b in attack["per_burst"] if b["gain_start_db"] is not None]
    if len(pts) < 3:
        return {"method": "burst onset gains", "points": pts}
    t = np.array([p[0] for p in pts]) - pts[0][0]
    g = np.array([p[1] for p in pts])
    return {"method": "burst onset gains",
            "onset_gain_db": [round(float(v), 3) for v in g],
            "spread_db": float(np.max(g) - np.min(g)),
            "fit": _fit_exp(t, g) if np.max(g) - np.min(g) > 0.2 else None}


def release_from_sine40(cap, ref, fs, layout, ainfo_cap, ainfo_ref, lp_hz=50.0):
    """(c) The -40 dBFS sine starts 1.5 s after the -20 dBFS sine stops.  If
    the release is slow the gain over its first 100 ms is still below its own
    steady value over the last 500 ms."""
    s = seg(layout, "sine40")
    off = send_offset_db(ainfo_cap, ainfo_ref)

    def g(t0, t1):
        ec = tone_envelope(cut(cap[:, 0], fs, t0, t1), fs, s.get("hz", 1000.0), lp_hz)
        er = tone_envelope(cut(ref[:, 0], fs, t0, t1), fs, s.get("hz", 1000.0), lp_hz)
        n = min(len(ec), len(er))
        return float(db(np.median(ec[:n])) - db(np.median(er[:n])) - off)

    early = g(s["start"] + 0.02, s["start"] + 0.12)
    late = g(s["end"] - 0.55, s["end"] - 0.05)
    return {"method": "-40 dBFS sine onset vs steady",
            "gain_first_100ms_db": early, "gain_steady_db": late,
            "still_recovering_db": early - late}


def steady_gains(cap, ref, fs, layout, ainfo_cap, ainfo_ref, lp_hz=50.0):
    off = send_offset_db(ainfo_cap, ainfo_ref)
    out = {}
    for name, skip in (("sine20", 0.5), ("sine40", 0.5)):
        s = seg(layout, name)
        ec = tone_envelope(cut(cap[:, 0], fs, s["start"] + skip, s["end"] - 0.1), fs, s.get("hz", 1000.0), lp_hz)
        er = tone_envelope(cut(ref[:, 0], fs, s["start"] + skip, s["end"] - 0.1), fs, s.get("hz", 1000.0), lp_hz)
        n = min(len(ec), len(er))
        out[name + "_gain_db"] = float(db(np.median(ec[:n])) - db(np.median(er[:n])) - off)
    return out


def click_gain(cap, ref, fs, layout, ainfo_cap, ainfo_ref):
    """Peak gain of each click, capture over reference.  A single-sample
    click has almost no rms energy, so a detector that follows rms leaves
    these at the small-signal gain while a peak detector pulls them down."""
    s = seg(layout, "clicks")
    off = send_offset_db(ainfo_cap, ainfo_ref)
    gs = []
    for tc in s["click_times"]:
        a = int(round((tc - 0.003) * fs))
        b = int(round((tc + 0.02) * fs))
        pc = np.abs(cap[a:b, 0]).max()
        pr = np.abs(ref[a:b, 0]).max()
        if pr > 0:
            gs.append(float(db(pc) - db(pr) - off))
    return {"click_peak_gain_db": [round(v, 3) for v in gs],
            "median_db": float(np.median(gs)) if gs else None}


# ------------------------------------------------------------------ per-cap

def analyze_one(cap_path, ref_path, signal, layout, outdir=None, plot=True,
                fit_lo=-38.0, fit_hi=-1.0):
    x, fs = load_wav(signal)
    x = x[:, 0]
    lay = load_layout(layout)
    cap, ainfo = load_aligned(cap_path, x, fs, lay)
    ref, rinfo = load_aligned(ref_path, x, fs, lay)
    if cap.shape[1] < 3 or ref.shape[1] < 3:
        raise SystemExit("both the capture and its reference need the 3-channel bench layout")

    curve = static_curve(cap, ref, fs, lay, ainfo, rinfo)
    st = fit_static(curve, fit_lo, fit_hi)
    atk = attack_from_bursts(cap, ref, fs, lay, ainfo, rinfo)
    rel_a = release_from_noise_floor(cap, ref, fs, lay, ainfo, rinfo)
    rel_b = release_from_burst_onsets(atk)
    rel_c = release_from_sine40(cap, ref, fs, lay, ainfo, rinfo)
    sg = steady_gains(cap, ref, fs, lay, ainfo, rinfo)
    ck = click_gain(cap, ref, fs, lay, ainfo, rinfo)

    name = os.path.splitext(os.path.basename(cap_path))[0]
    outdir = outdir or os.path.join(ROOT, "out", "comp")
    os.makedirs(outdir, exist_ok=True)
    csv_path = os.path.join(outdir, name + ".curve.csv")
    with open(csv_path, "w") as fh:
        fh.write("in_dbfs,out_db,chain_db,gain_db\n")
        for i in range(len(curve["x_dbfs"])):
            fh.write(f"{curve['x_dbfs'][i]:.4f},{curve['out_db'][i]:.4f},"
                     f"{curve['chain_db'][i]:.4f},{curve['gain_db'][i]:.4f}\n")
    png_path = None
    if plot:
        png_path = os.path.join(outdir, name + ".curve.png")
        _plot(name, curve, st, atk, png_path)

    return {"capture": cap_path, "reference": ref_path,
            "params_from_name": params_from_name(cap_path),
            "align": {"capture_offset_s": ainfo.get("offset_s"),
                      "reference_offset_s": rinfo.get("offset_s"),
                      "send_offset_db": send_offset_db(ainfo, rinfo)},
            "static": st,
            "attack": {k: v for k, v in atk.items() if k != "per_burst"},
            "attack_per_burst": [{k: v for k, v in b.items() if not k.startswith("_")}
                                 for b in atk["per_burst"]],
            "release_noise_floor": rel_a, "release_burst_onsets": rel_b,
            "release_sine40": rel_c,
            "steady": sg, "clicks": ck,
            "csv": csv_path, "png": png_path}


def _plot(name, curve, st, atk, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(1, 3, figsize=(15, 4.5))
    x = curve["x_dbfs"]
    yo = curve["out_db"] - curve["chain_db"] + curve["x_dbfs"]
    ax[0].plot(x, yo, lw=1.4, label="measured")
    if st:
        xs = np.linspace(x.min(), x.max(), 300)
        ax[0].plot(xs, soft_knee(xs, st["threshold_dbfs"], st["ratio"],
                                 st["knee_width_db"], st["makeup_db"]), "k--", lw=1.1, label="fit")
        ax[0].set_title(f"T {st['threshold_dbfs']:.2f} dBFS  R {st['ratio']:.2f}:1  "
                        f"knee {st['knee_width_db']:.2f} dB  makeup {st['makeup_db']:+.2f} dB\n"
                        f"resid {st['residual_rms_db']:.3f} dB rms", fontsize=8)
    ax[0].plot(x, x, color="0.7", lw=0.7)
    ax[0].set_xlabel("input dBFS"), ax[0].set_ylabel("output, input scale dB")
    ax[0].grid(alpha=0.3), ax[0].legend(fontsize=8)

    ax[1].plot(x, curve["gain_db"], lw=1.4)
    ax[1].set_xlabel("input dBFS"), ax[1].set_ylabel("gain dB")
    ax[1].set_title("gain vs level (ramp)", fontsize=8), ax[1].grid(alpha=0.3)

    for b in atk["per_burst"]:
        if len(b["_t"]):
            ax[2].plot(b["_t"] * 1000, b["_g"], lw=1.0, label=f"burst {b['burst']}")
    t = atk.get("attack_tau_ms_median")
    ax[2].set_title(f"attack: tau {t:.2f} ms" if t else "attack", fontsize=8)
    ax[2].set_xlabel("ms from burst start"), ax[2].set_ylabel("gain dB")
    ax[2].grid(alpha=0.3), ax[2].legend(fontsize=7)
    fig.suptitle(name, fontsize=9)
    fig.tight_layout()
    fig.savefig(path, dpi=110)
    plt.close(fig)


# -------------------------------------------------------------------- grid

def run_grid(grid_path, signal, layout, capture_dir, out_md, ref_path=None, outdir=None,
             fit_lo=-38.0, fit_hi=-1.0):
    fname = _capture_fname()
    with open(grid_path) as fh:
        grid = json.load(fh)
    def _path(row):
        # capture.py keeps the grid's key order; AX30G Bench writes the keys
        # sorted alphabetically (Attack, Level, Sensitivity). Accept either.
        for params in (row["params"], dict(sorted(row["params"].items()))):
            cand = os.path.join(capture_dir, fname(row["effect"], params, row.get("input", "N")))
            if os.path.exists(cand):
                return cand
        return os.path.join(capture_dir, fname(row["effect"], row["params"], row.get("input", "N")))
    paths = [(row, _path(row))
             for row in grid]
    if ref_path is None:
        zero = [p for row, p in paths
                if os.path.exists(p) and all(float(v) == 0 for v in row["params"].values())]
        ref_path = zero[0] if zero else DEFAULT_BYPASS
    if not os.path.exists(ref_path):
        raise SystemExit(f"no reference capture: {ref_path}")

    rows = []
    for row, p in paths:
        if not os.path.exists(p):
            rows.append({"params_from_name": row["params"], "missing": p})
            continue
        if os.path.abspath(p) == os.path.abspath(ref_path):
            rows.append({"params_from_name": row["params"], "is_reference": True, "capture": p})
            continue
        try:
            rows.append(analyze_one(p, ref_path, signal, layout, outdir, True, fit_lo, fit_hi))
        except Exception as e:                                   # noqa: BLE001
            rows.append({"params_from_name": row["params"], "error": f"{type(e).__name__}: {e}"})
    write_grid_md(rows, ref_path, grid_path, out_md, fit_lo, fit_hi)
    return rows


def _tag(r):
    p = r.get("params_from_name") or {}
    return ", ".join(f"{k} {p[k]:g}" for k in ("Sensitivity", "Level", "Attack") if k in p) or "?"


def write_grid_md(rows, ref_path, grid_path, out_md, fit_lo, fit_hi):
    d = datetime.date.today().isoformat()
    os.makedirs(os.path.dirname(os.path.abspath(out_md)), exist_ok=True)
    L = [f"# COMP static curve and time constants — {d}", "",
         f"Produced by `analysis/comp_curve.py --grid {os.path.relpath(grid_path, ROOT)}`.", "",
         f"- Reference capture (divided out, so the converter chain and the input stage cancel): "
         f"`{os.path.relpath(ref_path, ROOT)}`",
         f"- Static curve fitted over {fit_lo:g} to {fit_hi:g} dBFS of the 5 s ramp. The top of the "
         f"ramp is excluded: the stimulus reaches 0 dBFS, where the ADC hard clip "
         f"(`docs/input-stage-2026-09-16.md`) is only approximately cancelled by dividing two captures.",
         "- Attack is the exponential the gain follows inside the 60 ms bursts. Release is reported "
         "three ways because none of them is unconditionally reliable; see the notes under the table.",
         "- Every number carries its residual. Nothing here claims a match.", "",
         "| capture | thresh dBFS | ratio | knee dB | makeup dB | max GR dB | resid rms dB | attack τ ms | "
         "atk spread | release τ ms (floor) | exc/resid | onset spread dB | −40 onset−steady dB | "
         "gain −20 dB | gain −40 dB | click gain dB |",
         "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        t = _tag(r)
        if "missing" in r:
            L.append(f"| {t} | _not captured_ |" + " |" * 14)
            continue
        if r.get("is_reference"):
            L.append(f"| {t} | _reference row (divided out)_ |" + " |" * 14)
            continue
        if "error" in r:
            L.append(f"| {t} | _{r['error']}_ |" + " |" * 14)
            continue
        s = r["static"] or {}
        a, rn, rb, rc, sg, ck = (r["attack"], r["release_noise_floor"],
                                 r["release_burst_onsets"], r["release_sine40"],
                                 r["steady"], r["clicks"])

        def f(v, fmt="{:.3f}"):
            return "—" if v is None else fmt.format(v)
        L.append("| {t} | {T} | {R} | {W} | {M} | {GR} | {RR} | {AT} | {AS} | {RT} | {FM} | {OS} | {SR} "
                 "| {G20} | {G40} | {CK} |".format(
                     t=t, T=f(s.get("threshold_dbfs"), "{:+.2f}"), R=f(s.get("ratio"), "{:.2f}"),
                     W=f(s.get("knee_width_db"), "{:.2f}"), M=f(s.get("makeup_db"), "{:+.2f}"),
                     GR=f(s.get("max_gain_reduction_db"), "{:.2f}"),
                     RR=f(s.get("residual_rms_db"), "{:.3f}"),
                     AT=f(a.get("attack_tau_ms_median"), "{:.2f}"),
                     AS=f(a.get("attack_tau_ms_spread"), "{:.2f}"),
                     RT=f(rn.get("release_tau_ms_median"), "{:.0f}"),
                     FM=f(rn.get("excursion_over_residual"), "{:.1f}"),
                     OS=f(rb.get("spread_db"), "{:.2f}"),
                     SR=f(rc.get("still_recovering_db"), "{:+.2f}"),
                     G20=f(sg.get("sine20_gain_db"), "{:+.2f}"),
                     G40=f(sg.get("sine40_gain_db"), "{:+.2f}"),
                     CK=f(ck.get("median_db"), "{:+.2f}")))
    L += ["", "Per-capture curves are the `.curve.csv` and `.curve.png` files in `out/comp/`.", "",
          "## Reading the three release columns", "",
          "- **release τ ms (floor)** rides the device's own noise floor in the gap after each burst. "
          "**exc/resid** is the fitted gain excursion over the fit's own residual rms: under about 3 the "
          "time constant beside it is noise, not a measurement.",
          "- **onset spread dB** is the gain at burst 2/3/4 minus burst 1. Non-zero means the release is "
          "of the order of the 2.0 / 2.3 / 2.7 s burst spacings or slower; zero rules that out but says "
          "nothing about anything faster.",
          "- **−40 onset−steady dB** compares the first 100 ms of the −40 dBFS sine (1.5 s after the "
          "−20 dBFS sine stops) with its own steady value. Negative means still recovering at 1.5 s.",
          "", "## Reading the rest", "",
          "- **Sensitivity** is threshold if the threshold column moves with it and ratio does not; it is "
          "threshold *and* ratio if both move.",
          "- **Level** is makeup if `gain −40 dB` moves with it one-for-one and the threshold column "
          "does not move. If the threshold moves instead, Level sits before the detector.",
          "- **click gain dB** well below `gain −40 dB` means a peak detector; equal to it means rms.",
          "- The all-zero row is the block's own floor: if it is not flat and 0 dB against the bypass "
          "capture, COMP colours the signal with every control at zero and that offset belongs in the model.", ""]
    with open(out_md, "w") as fh:
        fh.write("\n".join(L) + "\n")
    return out_md


# -------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture", nargs="?")
    ap.add_argument("--ref", help="reference capture (default: the grid's all-zero row, else the LIN bypass)")
    ap.add_argument("--grid")
    ap.add_argument("--signal", default=DEFAULT_SIGNAL)
    ap.add_argument("--layout", default=DEFAULT_LAYOUT)
    ap.add_argument("--captures", default=CAPTURE_DIR)
    ap.add_argument("--out", help="grid mode: markdown path (default docs/comp-curve-<date>.md)")
    ap.add_argument("--outdir")
    ap.add_argument("--fit-lo", type=float, default=-38.0)
    ap.add_argument("--fit-hi", type=float, default=-1.0)
    ap.add_argument("--no-plot", action="store_true")
    ap.add_argument("--json")
    a = ap.parse_args()

    if a.grid:
        out = a.out or os.path.join(ROOT, "docs", f"comp-curve-{datetime.date.today().isoformat()}.md")
        rows = run_grid(a.grid, a.signal, a.layout, a.captures, out, a.ref, a.outdir, a.fit_lo, a.fit_hi)
        print(f"{sum(1 for r in rows if 'static' in r)}/{len(rows)} captures analysed -> {out}")
        return
    if not a.capture:
        ap.error("give a capture, or --grid")
    r = analyze_one(a.capture, a.ref or DEFAULT_BYPASS, a.signal, a.layout, a.outdir,
                    not a.no_plot, a.fit_lo, a.fit_hi)
    s, at = r["static"], r["attack"]
    print(os.path.basename(a.capture))
    print(f"  reference      {os.path.basename(r['reference'])}")
    if s:
        print(f"  threshold      {s['threshold_dbfs']:+.3f} dBFS")
        print(f"  ratio          {s['ratio']:.3f} : 1")
        print(f"  knee width     {s['knee_width_db']:.3f} dB")
        print(f"  makeup         {s['makeup_db']:+.3f} dB")
        print(f"  max reduction  {s['max_gain_reduction_db']:.3f} dB")
        print(f"  static resid   {s['residual_rms_db']:.4f} dB rms, {s['residual_max_db']:.4f} dB max "
              f"over {s['fit_band_dbfs'][0]:g}..{s['fit_band_dbfs'][1]:g} dBFS, n={s['n_points']}")
    if at.get("attack_tau_ms_median") is not None:
        print(f"  attack tau     {at['attack_tau_ms_median']:.3f} ms "
              f"(spread {at['attack_tau_ms_spread']:.3f} over {len(r['attack_per_burst'])} bursts, "
              f"depth {at['attack_depth_db_median']:+.3f} dB)")
    rn, rb, rc = r["release_noise_floor"], r["release_burst_onsets"], r["release_sine40"]
    if rn.get("release_tau_ms_median") is not None:
        print(f"  release (a)    {rn['release_tau_ms_median']:.1f} ms from the gap noise floor "
              f"(spread {rn['release_tau_ms_spread']:.1f} over n={rn['n']}; excursion "
              f"{rn['excursion_db']:.3f} dB over residual {rn['residual_rms_db']:.3f} dB = "
              f"{rn['excursion_over_residual']:.1f}x; floor {rn['floor_dbfs']:.1f} dBFS)")
    else:
        print("  release (a)    not measurable from the gap noise floor")
    print(f"  release (b)    burst onset gains {rb.get('onset_gain_db')}")
    print(f"  release (c)    -40 dBFS onset minus steady {rc['still_recovering_db']:+.3f} dB")
    print(f"  steady gain    -20 dBFS {r['steady']['sine20_gain_db']:+.3f} dB, "
          f"-40 dBFS {r['steady']['sine40_gain_db']:+.3f} dB")
    print(f"  click gain     {r['clicks']['median_db']:+.3f} dB median")
    print(f"  wrote {r['csv']}" + (f" and {r['png']}" if r["png"] else ""))
    if a.json:
        with open(a.json, "w") as fh:
            json.dump(r, fh, indent=1, default=float)


if __name__ == "__main__":
    main()
