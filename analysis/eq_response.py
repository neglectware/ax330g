#!/usr/bin/env python3
"""3BEQ: measure the EQ's frequency response from a capture and fit a
parametric model (low shelf + peaking mid + high shelf + broadband gain).

    eq_response.py CAPTURE.wav [--ref FLAT_OR_BYPASS.wav]
    eq_response.py --grid capture/grids/3beq.json [--out docs/3beq-response-<date>.md]

Method
------
Everything is a RATIO of two captures, never a ratio against the stimulus.
Each capture is three channels (AX30G L, AX30G R, reference loop of the
send, see README "Channels"), so within one capture the reference channel
cancels the Scarlett's own path and the send level.  Dividing the capture's
transfer function by the *flat* row's (or, failing that, the LIN bypass
capture's) then cancels the unit's converter chain -- the 3x 5.49 Hz
high-passes, the converter filters, the pre/de-emphasis pair -- all of which
are already measured and modelled elsewhere (models/ax30g-chain.json,
docs/input-stage-2026-09-16.md).  What is left is the EQ block alone.

Three independent estimates of that ratio:

  sweep   the 5 s log sweep, deconvolved against the reference channel
          (Farina: the log sweep puts harmonic distortion at negative time,
          so windowing the impulse response keeps only the linear part).
          This is the primary curve -- best signal-to-noise, and it carries
          phase.
  noise   Welch cross-spectrum H1 = Pxy/Pxx of L against the reference over
          the 3 s noise segment.  Magnitude only.  Different crest factor
          from the sweep, so a divergence between the two is evidence the
          stage is level-dependent (clipping), not evidence of a bad fit.
  clicks  the four single-sample clicks, windowed and FFT'd, averaged.
          Lowest signal-to-noise of the three (-16 dBFS, one sample), used
          as a sanity check on the sweep, mostly in the midrange.

Sample rates
------------
Captures are recorded at the Scarlett's rate and resampled to the signal
set's 48 kHz by analysis.run.load_aligned, so the measurement grid is a
48 kHz rfft grid in Hz.  The FITTED biquads are designed at the DEVICE rate
(39062.5 Hz nominal, --device-rate to change) because that is where the
model will run; a biquad designed at the device rate is evaluated on the
measured Hz grid, which is valid up to the device Nyquist, 19531 Hz.
Nothing above ~19 kHz is the EQ -- it is the converter chain's own roll-off
and whatever the two captures failed to cancel -- so the fit band stops at
18 kHz (--f-hi).

The fit is low shelf + peaking mid + high shelf + broadband gain, but it is
not simply fitted with all three bands free: see fit_model()'s docstring for
why every subset of the three is fitted and the smallest adequate one is the
one reported.
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
from scipy.signal import freqz, welch, csd

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)
from analysis.util import load_wav, load_layout, seg, cut, db      # noqa: E402
from analysis.run import load_aligned                              # noqa: E402

DEVICE_RATE = 39062.5           # nominal; the plugin runs here (see docs/sdly-clock-2026-09-16.md)
DEFAULT_SIGNAL = os.path.join(ROOT, "capture", "signalset-normal.wav")
DEFAULT_LAYOUT = os.path.join(ROOT, "capture", "layout-normal.json")
DEFAULT_BYPASS = os.path.join(ROOT, "captures", "AX30G_BYPASS_IN-LIN.wav")
CAPTURE_DIR = os.path.join(ROOT, "captures")

PARAM_RE = re.compile(r"([A-Za-z]+)-(-?\d+(?:\.\d+)?)")
LONG_NAMES = {"Bass": "Bass", "MidFreq": "Mid Freq", "MidGain": "Mid Gain",
              "Treble": "Treble", "Trim": "Trim"}


# ---------------------------------------------------------------- filenames

def _capture_fname():
    """capture/capture.py's own fname(), imported rather than copied so the
    naming convention has exactly one definition in the repo."""
    p = os.path.join(ROOT, "capture", "capture.py")
    spec = importlib.util.spec_from_file_location("_ax_capture", p)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m.fname


def params_from_name(name):
    out = {}
    body = os.path.basename(name)
    body = body.split("_IN-")[0]
    for k, v in PARAM_RE.findall(body):
        if k in LONG_NAMES:
            out[LONG_NAMES[k]] = float(v) if "." in v else int(v)
    return out


# ------------------------------------------------------------- measurement

def _deconv(y, x, fs, f_lo, f_hi, reg=1e-3):
    """Regularised deconvolution y/x, returned as a time-domain response."""
    n = 1 << int(np.ceil(np.log2(len(y) + len(x))))
    Y = np.fft.rfft(y, n)
    X = np.fft.rfft(x, n)
    f = np.fft.rfftfreq(n, 1.0 / fs)
    P = np.abs(X) ** 2
    band = (f >= f_lo) & (f <= f_hi)
    eps = reg * P[band].max()
    H = Y * np.conj(X) / (P + eps)
    return np.fft.irfft(H, n), n


def sweep_transfer(cap, fs, layout, channel=0, ref_channel=2,
                   pre_ms=20.0, post_ms=250.0, f_lo=20.0, f_hi=19000.0):
    """Complex transfer function of `channel` over the reference channel,
    from the sweep segment.  Returns (f, H).

    The impulse response is windowed -pre_ms .. +post_ms around t=0.  The
    window has to be long: the unit's own chain carries three 5.49 Hz
    high-passes whose impulse response decays with a ~29 ms time constant,
    and truncating that would put a false low-frequency tilt into every
    curve.  It also has to be short enough to exclude the log sweep's
    harmonic-distortion arrivals, which for a 5 s 20 Hz-19 kHz sweep sit at
    -0.5 s (2nd) and earlier.
    """
    s = seg(layout, "sweep")
    t0, t1 = s["start"] - 0.05, s["end"] + post_ms / 1000.0 + 0.2
    y = cut(cap[:, channel], fs, t0, t1)
    x = cut(cap[:, ref_channel], fs, t0, t1)
    ir_full, n = _deconv(y, x, fs, f_lo, f_hi)
    pre = int(round(pre_ms * fs / 1000.0))
    post = int(round(post_ms * fs / 1000.0))
    ir = np.concatenate([ir_full[-pre:], ir_full[:post]])
    w = np.ones(len(ir))
    e = int(round(0.005 * fs))
    ramp = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, e))
    w[:e] = ramp
    w[-e:] = ramp[::-1]
    ir = ir * w
    nfft = 1 << int(np.ceil(np.log2(len(ir))))
    H = np.fft.rfft(np.roll(np.concatenate([ir, np.zeros(nfft - len(ir))]), -pre), nfft)
    f = np.fft.rfftfreq(nfft, 1.0 / fs)
    return f, H


def noise_transfer(cap, fs, layout, channel=0, ref_channel=2, nperseg=8192):
    """H1 = Pxy/Pxx over the noise segment.  Complex, but only the magnitude
    is trusted (the segment is 3 s of stationary noise; the phase estimate is
    fine in the mid band and noisy at both ends)."""
    s = seg(layout, "noise")
    y = cut(cap[:, channel], fs, s["start"] + 0.3, s["end"] - 0.3)
    x = cut(cap[:, ref_channel], fs, s["start"] + 0.3, s["end"] - 0.3)
    f, Pxx = welch(x, fs, nperseg=nperseg)
    _, Pxy = csd(x, y, fs, nperseg=nperseg)
    return f, Pxy / np.maximum(Pxx, 1e-30)


def click_transfer(cap, fs, layout, channel=0, ref_channel=2, win_ms=60.0):
    """Average transfer function over the four clicks.  Each click is
    windowed in both channels from 5 ms before the reference peak to
    win_ms after, and the two spectra are divided (regularised)."""
    s = seg(layout, "clicks")
    accY = accX = None
    nfft = 1 << int(np.ceil(np.log2((win_ms + 10.0) * fs / 1000.0)))
    for tc in s["click_times"]:
        i = int(round(tc * fs))
        w = int(0.004 * fs)
        seg_ref = cap[i - w:i + w, ref_channel]
        if len(seg_ref) == 0:
            continue
        ip = i - w + int(np.argmax(np.abs(seg_ref)))
        a = ip - int(0.005 * fs)
        b = a + nfft
        if a < 0 or b > len(cap):
            continue
        Y = np.fft.rfft(cap[a:b, channel], nfft)
        X = np.fft.rfft(cap[a:b, ref_channel], nfft)
        accY = Y * np.conj(X) if accY is None else accY + Y * np.conj(X)
        accX = np.abs(X) ** 2 if accX is None else accX + np.abs(X) ** 2
    f = np.fft.rfftfreq(nfft, 1.0 / fs)
    eps = 1e-3 * accX[(f >= 100) & (f <= 15000)].max()
    return f, accY / (accX + eps)


def _interp_complex(f_src, H_src, f_dst):
    re = np.interp(f_dst, f_src, H_src.real)
    im = np.interp(f_dst, f_src, H_src.imag)
    return re + 1j * im


def log_grid(f_lo, f_hi, n=400):
    return np.geomspace(f_lo, f_hi, n)


def measure(cap, ref, fs, layout, f_lo=20.0, f_hi=19000.0, n_points=400):
    """The EQ's own response: (capture / its reference channel) divided by
    (reference capture / its reference channel), on a log frequency grid.

    Returns a dict with the log grid, the three estimates, and the
    disagreement between them.
    """
    fg = log_grid(f_lo, f_hi, n_points)
    out = {"f": fg}

    fs_, Hc = sweep_transfer(cap, fs, layout)
    fr_, Hr = sweep_transfer(ref, fs, layout)
    Hsw = _interp_complex(fs_, Hc, fg) / _interp_complex(fr_, Hr, fg)
    out["sweep"] = Hsw

    fn_, Hcn = noise_transfer(cap, fs, layout)
    fn2, Hrn = noise_transfer(ref, fs, layout)
    out["noise"] = _interp_complex(fn_, Hcn, fg) / _interp_complex(fn2, Hrn, fg)

    fk_, Hck = click_transfer(cap, fs, layout)
    _, Hrk = click_transfer(ref, fs, layout)
    out["clicks"] = _interp_complex(fk_, Hck, fg) / _interp_complex(fk_, Hrk, fg)

    band = (fg >= 50.0) & (fg <= 18000.0)
    for k in ("noise", "clicks"):
        d = db(np.abs(out[k])) - db(np.abs(out["sweep"]))
        out[k + "_vs_sweep_rms_db"] = float(np.sqrt(np.mean(d[band] ** 2)))
        out[k + "_vs_sweep_max_db"] = float(np.max(np.abs(d[band])))
    return out


# ------------------------------------------------------------------- model

def _shelf(kind, fs, fc, q, gain_db):
    A = 10 ** (gain_db / 40.0)
    w0 = 2 * np.pi * fc / fs
    cw, sw = np.cos(w0), np.sin(w0)
    alpha = sw / (2 * max(q, 1e-6))
    sa = 2 * np.sqrt(A) * alpha
    if kind == "low":
        b = [A * ((A + 1) - (A - 1) * cw + sa), 2 * A * ((A - 1) - (A + 1) * cw),
             A * ((A + 1) - (A - 1) * cw - sa)]
        a = [(A + 1) + (A - 1) * cw + sa, -2 * ((A - 1) + (A + 1) * cw),
             (A + 1) + (A - 1) * cw - sa]
    else:
        b = [A * ((A + 1) + (A - 1) * cw + sa), -2 * A * ((A - 1) + (A + 1) * cw),
             A * ((A + 1) + (A - 1) * cw - sa)]
        a = [(A + 1) - (A - 1) * cw + sa, 2 * ((A - 1) - (A + 1) * cw),
             (A + 1) - (A - 1) * cw - sa]
    return np.array(b) / a[0], np.array(a) / a[0]


def _peak(fs, fc, q, gain_db):
    A = 10 ** (gain_db / 40.0)
    w0 = 2 * np.pi * fc / fs
    cw, sw = np.cos(w0), np.sin(w0)
    alpha = sw / (2 * max(q, 1e-6))
    b = [1 + alpha * A, -2 * cw, 1 - alpha * A]
    a = [1 + alpha / A, -2 * cw, 1 - alpha / A]
    return np.array(b) / a[0], np.array(a) / a[0]


def model_response(p, f, fs):
    """p = [gain_db, fc_low, q_low, g_low, fc_mid, q_mid, g_mid, fc_high,
    q_high, g_high].  Returns complex H on the Hz grid f."""
    g0, fl, ql, gl, fm, qm, gm, fh, qh, gh = p
    H = np.full(len(f), 10 ** (g0 / 20.0), dtype=complex)
    for b, a in (_shelf("low", fs, fl, ql, gl), _peak(fs, fm, qm, gm),
                 _shelf("high", fs, fh, qh, gh)):
        _, h = freqz(b, a, worN=2 * np.pi * f / fs)
        H = H * h
    return H


P_NAMES = ["gain_db", "low_fc_hz", "low_q", "low_gain_db",
           "mid_fc_hz", "mid_q", "mid_gain_db",
           "high_fc_hz", "high_q", "high_gain_db"]


BANDS = ("low", "mid", "high")
SEED_FC = {"low": (80.0, 200.0, 500.0), "mid": (300.0, 800.0, 2000.0, 5000.0),
           "high": (1500.0, 4000.0, 9000.0)}
BAND_BOUNDS = {"low": (20.0, 1500.0, 0.15, 4.0), "mid": (100.0, 12000.0, 0.15, 6.0),
               "high": (500.0, 18000.0, 0.15, 4.0)}


def _expand(p, active):
    """Reduced vector -> the full 10-parameter vector model_response wants."""
    full = [p[0], 1000.0, 0.7, 0.0, 1000.0, 1.0, 0.0, 4000.0, 0.7, 0.0]
    i = 1
    for k, band in enumerate(BANDS):
        if band in active:
            full[1 + 3 * k], full[2 + 3 * k], full[3 + 3 * k] = p[i], p[i + 1], p[i + 2]
            i += 3
    return full


def _fit_subset(ff, y, fs_device, active):
    """Least squares with exactly the bands in `active` free; the others are
    held at 0 dB, which takes their fc and Q out of the problem entirely."""
    lo = [-40.0]
    hi = [40.0]
    for band in BANDS:
        if band in active:
            f0, f1, q0, q1 = BAND_BOUNDS[band]
            lo += [f0, q0, -30.0]
            hi += [f1, q1, 30.0]

    def resid(p):
        return db(np.abs(model_response(_expand(p, active), ff, fs_device))) - y

    g0 = float(np.median(y[(ff > 800) & (ff < 1500)])) if ((ff > 800) & (ff < 1500)).any() else 0.0
    guess = {"low": float(np.median(y[ff < 120])) - g0 if (ff < 120).any() else 0.0,
             "mid": 0.0,
             "high": float(np.median(y[ff > 8000])) - g0 if (ff > 8000).any() else 0.0}
    combos = [()]
    for band in BANDS:
        if band in active:
            combos = [c + (fc,) for c in combos for fc in SEED_FC[band]]
    best = None
    for combo in combos:
        p0 = [g0]
        j = 0
        for band in BANDS:
            if band in active:
                p0 += [combo[j], 0.9 if band == "mid" else 0.7, guess[band]]
                j += 1
        p0 = [min(max(v, l), h) for v, l, h in zip(p0, lo, hi)]
        try:
            r = least_squares(resid, p0, bounds=(lo, hi), max_nfev=3000)
        except Exception:
            continue
        rms = float(np.sqrt(np.mean(r.fun ** 2)))
        if best is None or rms < best[1]:
            best = (r.x, rms, r.fun)
    return best


def fit_model(f, H_meas, fs_device, f_lo=30.0, f_hi=18000.0,
              rel_tol=0.25, abs_tol=0.01):
    """Fit low shelf + peaking mid + high shelf + broadband gain to the
    measured MAGNITUDE in dB over [f_lo, f_hi], on a log-spaced grid so every
    octave carries the same weight.

    Three bands are more than most rows of the 3BEQ grid need, and an
    over-parameterised fit is worse than useless here: the optimiser parks a
    spurious few tenths of a dB in an unused band, right on top of the band
    that is really moving, and the per-band numbers stop meaning anything
    while the residual stays tiny.  (An L2 ridge on the band gains makes this
    worse, not better -- it prefers spreading one 10 dB band over three.)
    So every subset of the three bands is fitted and the SMALLEST one whose
    residual is within `rel_tol` (relative) plus `abs_tol` dB of the full
    three-band fit's is the one reported.  Bands left out are reported at
    0.00 dB with fc and Q as None: they are not doing anything, so their
    fc and Q are not measurable from this capture.

    Returns (params dict, residual rms dB, residual array, band mask).
    """
    m = (f >= f_lo) & (f <= f_hi)
    y = db(np.abs(H_meas))[m]
    ff = f[m]

    subsets = [(), ("low",), ("mid",), ("high",), ("low", "mid"), ("low", "high"),
               ("mid", "high"), ("low", "mid", "high")]
    fits = {}
    for act in subsets:
        r = _fit_subset(ff, y, fs_device, act)
        if r is not None:
            fits[act] = r
    full_rms = fits[("low", "mid", "high")][1]
    limit = full_rms * (1.0 + rel_tol) + abs_tol
    chosen = None
    for act in subsets:
        if act in fits and fits[act][1] <= limit:
            chosen = act
            break
    if chosen is None:
        chosen = ("low", "mid", "high")
    p, rms, res = fits[chosen]
    full = _expand(p, chosen)
    out = dict(zip(P_NAMES, [float(v) for v in full]))
    for k, band in enumerate(BANDS):
        if band not in chosen:
            out[band + "_fc_hz"] = None
            out[band + "_q"] = None
            out[band + "_gain_db"] = 0.0
    out["_bands_active"] = list(chosen)
    out["_full_model_residual_rms_db"] = full_rms
    return out, rms, res, m


# ------------------------------------------------------------------ per-cap

def analyze_one(cap_path, ref_path, signal, layout, fs_device=DEVICE_RATE,
                f_lo=30.0, f_hi=18000.0, outdir=None, plot=True):
    x, fs = load_wav(signal)
    x = x[:, 0]
    lay = load_layout(layout)
    cap, ainfo = load_aligned(cap_path, x, fs, lay)
    ref, rinfo = load_aligned(ref_path, x, fs, lay)
    if cap.shape[1] < 3 or ref.shape[1] < 3:
        raise SystemExit("both the capture and its reference need the 3-channel "
                         "bench layout (AX30G L, AX30G R, reference loop)")

    meas = measure(cap, ref, fs, lay)
    f = meas["f"]
    H = meas["sweep"]
    params, rms, res, mask = fit_model(f, H, fs_device, f_lo, f_hi)

    # phase: measured vs the fitted (minimum-phase) model, as a diagnostic
    Hm = model_response([params[k] if params[k] is not None else 1000.0 if k.endswith("_hz") else 0.7
                         for k in P_NAMES], f, fs_device)
    ph_meas = np.unwrap(np.angle(H))
    ph_mod = np.unwrap(np.angle(Hm))
    pb = (f >= 100.0) & (f <= 10000.0)
    ph_res = np.degrees(ph_meas - ph_mod)
    ph_res = ph_res - np.median(ph_res[pb])

    name = os.path.splitext(os.path.basename(cap_path))[0]
    outdir = outdir or os.path.join(ROOT, "out", "eq")
    os.makedirs(outdir, exist_ok=True)
    csv_path = os.path.join(outdir, name + ".response.csv")
    with open(csv_path, "w") as fh:
        fh.write("hz,mag_db_sweep,phase_deg_sweep,mag_db_noise,mag_db_clicks,mag_db_fit\n")
        mag_fit = db(np.abs(Hm))
        for i in range(len(f)):
            fh.write(f"{f[i]:.3f},{db(abs(H[i])):.5f},{np.degrees(ph_meas[i]):.4f},"
                     f"{db(abs(meas['noise'][i])):.5f},{db(abs(meas['clicks'][i])):.5f},"
                     f"{mag_fit[i]:.5f}\n")

    png_path = None
    if plot:
        png_path = os.path.join(outdir, name + ".response.png")
        _plot(name, f, meas, Hm, params, rms, png_path)

    return {
        "capture": cap_path, "reference": ref_path,
        "params_from_name": params_from_name(cap_path),
        "align": {"capture_offset_s": ainfo.get("offset_s"),
                  "reference_offset_s": rinfo.get("offset_s"),
                  "capture_send_db": ainfo.get("ref_gain_db"),
                  "reference_send_db": rinfo.get("ref_gain_db")},
        "fit": params, "fit_residual_rms_db": rms,
        "fit_residual_max_db": float(np.max(np.abs(res))),
        "fit_band_hz": [f_lo, f_hi],
        "device_rate_hz": fs_device,
        "cross_check": {k: meas[k] for k in meas if k.endswith("_db")},
        "phase_residual_rms_deg_100_10k": float(np.sqrt(np.mean(ph_res[pb] ** 2))),
        "csv": csv_path, "png": png_path,
    }


def _f(v, fmt="{:.2f}", dash="—"):
    return dash if v is None else fmt.format(v)


def _band_str(params, band):
    g = params[band + "_gain_db"]
    if params[band + "_fc_hz"] is None:
        return f"{band} not needed"
    return f"{band} {params[band + '_fc_hz']:.0f} Hz Q{params[band + '_q']:.2f} {g:+.2f} dB"


def _plot(name, f, meas, Hm, params, rms, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(2, 1, figsize=(10, 7), sharex=True)
    ax[0].semilogx(f, db(np.abs(meas["sweep"])), label="sweep", lw=1.6)
    ax[0].semilogx(f, db(np.abs(meas["noise"])), label="noise", lw=0.9, alpha=0.75)
    ax[0].semilogx(f, db(np.abs(meas["clicks"])), label="clicks", lw=0.8, alpha=0.6)
    ax[0].semilogx(f, db(np.abs(Hm)), "k--", label="fit", lw=1.2)
    ax[0].set_ylabel("dB")
    ax[0].grid(True, which="both", alpha=0.3)
    ax[0].legend(fontsize=8, ncol=4)
    ax[0].set_title(f"{name}\nfit rms {rms:.3f} dB | " + " | ".join(
        _band_str(params, b) for b in ("low", "mid", "high")), fontsize=8)
    ax[1].semilogx(f, db(np.abs(meas["sweep"])) - db(np.abs(Hm)), lw=1.0)
    ax[1].axhline(0, color="k", lw=0.5)
    ax[1].set_ylabel("residual dB")
    ax[1].set_xlabel("Hz")
    ax[1].set_xlim(20, 20000)
    ax[1].grid(True, which="both", alpha=0.3)
    fig.tight_layout()
    fig.savefig(path, dpi=110)
    plt.close(fig)


# -------------------------------------------------------------------- grid

def run_grid(grid_path, signal, layout, capture_dir, out_md, fs_device=DEVICE_RATE,
             f_lo=30.0, f_hi=18000.0, ref_path=None, outdir=None):
    fname = _capture_fname()
    with open(grid_path) as fh:
        grid = json.load(fh)
    paths = []
    for row in grid:
        p = os.path.join(capture_dir, fname(row["effect"], row["params"], row.get("input", "N")))
        paths.append((row, p, os.path.exists(p)))

    # the reference is the grid's flat row if it was captured, else the LIN bypass
    if ref_path is None:
        flat = [p for row, p, ok in paths
                if ok and all(float(v) == 0 for k, v in row["params"].items() if k != "Mid Freq")]
        ref_path = flat[0] if flat else DEFAULT_BYPASS
    if not os.path.exists(ref_path):
        raise SystemExit(f"no reference capture: {ref_path}")

    rows = []
    for row, p, ok in paths:
        if not ok:
            rows.append({"params_from_name": row["params"], "missing": p})
            continue
        if os.path.abspath(p) == os.path.abspath(ref_path):
            rows.append({"params_from_name": row["params"], "is_reference": True,
                         "capture": p})
            continue
        try:
            rows.append(analyze_one(p, ref_path, signal, layout, fs_device, f_lo, f_hi, outdir))
        except Exception as e:                                  # noqa: BLE001
            rows.append({"params_from_name": row["params"], "error": f"{type(e).__name__}: {e}"})

    write_grid_md(rows, ref_path, grid_path, out_md, f_lo, f_hi, fs_device)
    return rows


def write_grid_md(rows, ref_path, grid_path, out_md, f_lo, f_hi, fs_device):
    d = datetime.date.today().isoformat()
    os.makedirs(os.path.dirname(os.path.abspath(out_md)), exist_ok=True)
    L = []
    L.append(f"# 3BEQ frequency response — {d}")
    L.append("")
    L.append(f"Produced by `analysis/eq_response.py --grid {os.path.relpath(grid_path, ROOT)}`.")
    L.append("")
    L.append(f"- Reference capture (divided out, so the converter chain cancels): "
             f"`{os.path.relpath(ref_path, ROOT)}`")
    L.append(f"- Fit band {f_lo:g}–{f_hi:g} Hz; biquads designed at {fs_device:g} Hz "
             f"(the device rate — the fit is what the plugin block will run).")
    L.append("- Above ~19 kHz is the converter chain, not the EQ; it is outside the fit band "
             "and outside the plots' meaning.")
    L.append("- Every number is a measurement with its residual beside it. Nothing here claims a match.")
    L.append("")
    L.append("| capture | bands | gain dB | low fc Hz | low Q | low dB | mid fc Hz | mid Q | mid dB "
             "| high fc Hz | high Q | high dB | resid rms dB | resid max dB | noise−sweep rms dB | clicks−sweep rms dB |")
    L.append("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for r in rows:
        tag = _row_tag(r)
        if "missing" in r:
            L.append(f"| {tag} | _not captured_ |" + " |" * 14)
            continue
        if r.get("is_reference"):
            L.append(f"| {tag} | _reference row (divided out)_ |" + " |" * 14)
            continue
        if "error" in r:
            L.append(f"| {tag} | _{r['error']}_ |" + " |" * 14)
            continue
        p = r["fit"]
        cc = r["cross_check"]
        L.append("| {t} | {ba} | {g} | {lf} | {lq} | {lg} | {mf} | {mq} | {mg} "
                 "| {hf} | {hq} | {hg} | {rr} | {rm} | {nn} | {ck} |".format(
                     t=tag, ba="+".join(p.get("_bands_active") or []) or "none",
                     g=_f(p["gain_db"], "{:+.2f}"),
                     lf=_f(p["low_fc_hz"], "{:.1f}"), lq=_f(p["low_q"]), lg=_f(p["low_gain_db"], "{:+.2f}"),
                     mf=_f(p["mid_fc_hz"], "{:.1f}"), mq=_f(p["mid_q"]), mg=_f(p["mid_gain_db"], "{:+.2f}"),
                     hf=_f(p["high_fc_hz"], "{:.1f}"), hq=_f(p["high_q"]), hg=_f(p["high_gain_db"], "{:+.2f}"),
                     rr=_f(r["fit_residual_rms_db"], "{:.3f}"), rm=_f(r["fit_residual_max_db"], "{:.3f}"),
                     nn=_f(cc.get("noise_vs_sweep_rms_db"), "{:.3f}"),
                     ck=_f(cc.get("clicks_vs_sweep_rms_db"), "{:.3f}")))
    L.append("")
    L.append("`bands` is which of the three bands the fit actually needed. Every subset of the three is "
             "fitted and the smallest one whose residual is within 25 % + 0.01 dB of the full three-band "
             "fit’s is reported; a band left out shows 0.00 dB with fc and Q as —, because a band that is "
             "not moving has no measurable fc or Q. Without that selection the optimiser parks a spurious "
             "few tenths of a dB in an unused band, right on top of the band that is really moving, and "
             "the per-band numbers stop meaning anything while the residual stays tiny.")
    L.append("")
    L.append("Per-capture magnitude/phase curves are the `.response.csv` and `.response.png` "
             "files in `out/eq/`.")
    L.append("")
    L.append("## What to read out of this")
    L.append("")
    L.append("- **Trim** is a plain gain if the three Trim rows differ from the flat row only in "
             "`gain dB` and their shelf/peak gains stay near 0.")
    L.append("- **Pre- vs post-EQ Trim**: compare the `Trim −18 + Bass +16` row's shape against the "
             "`Bass +16` row's after removing 18 dB. A linear EQ and a scalar commute, so a shape "
             "difference means one of the two clipped inside the block.")
    L.append("- **Q versus gain**: constant Q ⇒ the `Mid Gain +8` row's `mid_q` equals the `+16` "
             "row's; proportional Q ⇒ the +8 row reads a lower Q.")
    L.append("- **Level dependence**: `noise−sweep rms dB` is the disagreement between two segments "
             "of different crest factor measured through the same setting. It is a few hundredths of "
             "a dB on a clean linear stage; a large value on the all-boost row is the clipping answer.")
    L.append("")
    with open(out_md, "w") as fh:
        fh.write("\n".join(L) + "\n")
    return out_md


def _row_tag(r):
    p = r.get("params_from_name") or {}
    order = ["Bass", "Mid Freq", "Mid Gain", "Treble", "Trim"]
    bits = [f"{k} {p[k]:g}" for k in order if k in p]
    return ", ".join(bits) if bits else "?"


# -------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture", nargs="?", help="one capture WAV (3 channels)")
    ap.add_argument("--ref", help="reference capture: the grid's flat row, else the LIN bypass")
    ap.add_argument("--grid", help="run over every capture of a grid JSON and write a markdown table")
    ap.add_argument("--signal", default=DEFAULT_SIGNAL)
    ap.add_argument("--layout", default=DEFAULT_LAYOUT)
    ap.add_argument("--captures", default=CAPTURE_DIR)
    ap.add_argument("--out", help="grid mode: markdown path (default docs/3beq-response-<date>.md)")
    ap.add_argument("--outdir", help="per-capture CSV/PNG directory (default out/eq)")
    ap.add_argument("--device-rate", type=float, default=DEVICE_RATE)
    ap.add_argument("--f-lo", type=float, default=30.0)
    ap.add_argument("--f-hi", type=float, default=18000.0)
    ap.add_argument("--no-plot", action="store_true")
    ap.add_argument("--json", help="write the per-capture result dict here")
    a = ap.parse_args()

    if a.grid:
        out = a.out or os.path.join(ROOT, "docs", f"3beq-response-{datetime.date.today().isoformat()}.md")
        rows = run_grid(a.grid, a.signal, a.layout, a.captures, out, a.device_rate,
                        a.f_lo, a.f_hi, a.ref, a.outdir)
        done = sum(1 for r in rows if "fit" in r)
        print(f"{done}/{len(rows)} captures analysed -> {out}")
        return
    if not a.capture:
        ap.error("give a capture, or --grid")
    ref = a.ref or DEFAULT_BYPASS
    r = analyze_one(a.capture, ref, a.signal, a.layout, a.device_rate, a.f_lo, a.f_hi,
                    a.outdir, plot=not a.no_plot)
    p = r["fit"]
    print(f"{os.path.basename(a.capture)}")
    print(f"  reference      {os.path.basename(ref)}")
    print(f"  gain           {p['gain_db']:+.3f} dB")
    for label, band in (("low shelf", "low"), ("mid peak", "mid"), ("high shelf", "high")):
        print(f"  {label:14s} {_f(p[band + '_fc_hz'], '{:8.1f}'):>8s} Hz  "
              f"Q {_f(p[band + '_q'], '{:.3f}'):>6s}  {p[band + '_gain_db']:+.3f} dB"
              + ("   (band not needed by the fit: fc/Q not measurable here)" if p[band + "_fc_hz"] is None else ""))
    print(f"  residual       {r['fit_residual_rms_db']:.4f} dB rms, "
          f"{r['fit_residual_max_db']:.4f} dB max, over {a.f_lo:g}-{a.f_hi:g} Hz")
    cc = r["cross_check"]
    print(f"  cross-check    noise-sweep {cc['noise_vs_sweep_rms_db']:.3f} dB rms "
          f"({cc['noise_vs_sweep_max_db']:.3f} max), clicks-sweep "
          f"{cc['clicks_vs_sweep_rms_db']:.3f} dB rms ({cc['clicks_vs_sweep_max_db']:.3f} max)")
    print(f"  phase residual {r['phase_residual_rms_deg_100_10k']:.2f} deg rms, 100 Hz-10 kHz "
          f"(vs the minimum-phase fit)")
    print(f"  wrote {r['csv']}" + (f" and {r['png']}" if r["png"] else ""))
    if a.json:
        with open(a.json, "w") as fh:
            json.dump(r, fh, indent=1, default=float)


if __name__ == "__main__":
    main()
