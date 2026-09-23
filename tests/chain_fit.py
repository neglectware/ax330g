"""Parametric fit of the linear "chain" around the Stereo Delay: an LF stage
(the wet-path low-frequency signature below ~200 Hz) and an HF stage (the
converters' filter phase/magnitude near 19.5 kHz, missing from the model's
resampler). Both are evaluated with the same align + one-gain-fit null test
as tests/null_test.py (its own functions are imported, not reimplemented).

Key shortcut used throughout: in render_sdly (engine/effects.py), the
Balance/dry weights are applied ONLY at the final per-sample mix
`out[i] = dry*xi + wet*r` -- they do not touch the delay line, the feedback
gain or the damp filter. So rendering the model with L Bal/R Bal forced to
50 (wet=1, dry=0) at whatever L Fb/R Fb/High Damp the target capture used
yields the *raw* wet-path signal r on its own, with the *same* internal loop
dynamics (delay, feedback, damp) the real capture's own Balance would have
driven. That raw r is rendered once per (Fb, Damp) combination; every
candidate LF filter is then a cheap post-filter (scipy.signal.lfilter/sosfilt)
on that one render, and the full mixed output for any Balance is rebuilt as
dry(bal)*model_conv + wet(bal)*filtered(r) with model_conv the converters-only
render (L Bal=R Bal=0 is dry=1,wet=0, one render, reused for the HF fit too).
"apply as if inside the loop" (compounding across repeats) genuinely needs a
fresh per-sample render, since the filter then sits inside the feedback
recursion; that path uses the model's own already-implemented
blocks.delay_line.input_hpf_hz for the order-1 case, and a small local
re-implementation of render_sdly (not touching engine/effects.py) for order-2.

Usage: .venv/bin/python tests/chain_fit.py [--out docs/chain-fit-2026-09-13.md]
"""
import argparse
import copy
import math
import os
import sys
import numpy as np
import soundfile as sf
from scipy.signal import fftconvolve, lfilter, sosfilt, sosfiltfilt, butter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
from analysis.util import load_layout, seg, cut          # noqa: E402
from analysis.run import load_aligned                     # noqa: E402
from engine.render import load_spec, render_spec           # noqa: E402
from engine.params import apply_map                        # noqa: E402
from engine.blocks import DelayLine, Storage, OnePoleLP, Gain  # noqa: E402
from tests.null_test import params_from_name, db, best_shift, frac_shift  # noqa: E402

MODEL_PATH = os.path.join(ROOT, "models", "ax30g-sdly.json")
SIGNAL_PATH = os.path.join(ROOT, "capture", "signalset.wav")
LAYOUT_PATH = os.path.join(ROOT, "capture", "layout.json")
EQ_BASELINE = os.path.join(ROOT, "out", "null", "chain-eq-bypass.npy")

CAP_DRY = os.path.join(ROOT, "captures", "AX30G_SDLY_LDly-300_RDly-300_LFb-0_RFb-0_HighDamp-0_LBal-0_RBal-0_IN-LIN.wav")
CAP_WET = os.path.join(ROOT, "captures", "AX30G_SDLY_LDly-300_RDly-300_LFb-0_RFb-0_HighDamp-0_LBal-50_RBal-50_IN-LIN.wav")
CAP_FB25 = os.path.join(ROOT, "captures", "AX30G_SDLY_LDly-300_RDly-300_LFb-25_RFb-25_HighDamp-0_LBal-25_RBal-25_IN-LIN.wav")
CAP_BYPASS = os.path.join(ROOT, "captures", "AX30G_BYPASS_IN-LIN.wav")

LF_BANDS = [("20-40 Hz", 20, 40), ("40-80 Hz", 40, 80), ("80-160 Hz", 80, 160),
            ("160-320 Hz", 160, 320), ("320-640 Hz", 320, 640)]
HF_BANDS = [("12k-16k Hz", 12000, 16000), ("16k-18k Hz", 16000, 18000), ("18k-19.5k Hz", 18000, 19500)]


# ---------------------------------------------------------------- utilities

def load_signal():
    x, fs = sf.read(SIGNAL_PATH, dtype="float64", always_2d=True)
    return x[:, 0], float(fs)


def align_gain_residual(c, m, fs, layout):
    """Exactly null_test.py's per-channel step: align on the sweep, fit one
    gain over everything but the ramp, return the mask, aligned model and
    residual."""
    sw = seg(layout, "sweep")
    lag = best_shift(cut(c, fs, sw["start"], sw["end"]), cut(m, fs, sw["start"], sw["end"]), fs)
    m2 = frac_shift(m, lag)
    mask = np.ones(len(c), bool)
    rp = seg(layout, "ramp")
    mask[int(rp["start"] * fs):int(rp["end"] * fs)] = False
    g = float(np.dot(c[mask], m2[mask]) / max(np.dot(m2[mask], m2[mask]), 1e-20))
    r = c - g * m2
    return lag, g, mask, m2, r


def seg_table(c, r, fs, layout):
    rows = []
    for s in layout["segments"]:
        if s["name"] in ("silence",):
            continue
        cs, rs = cut(c, fs, s["start"], s["end"]), cut(r, fs, s["start"], s["end"])
        rows.append((s["name"], db(np.sqrt(np.mean(cs ** 2))), db(np.sqrt(np.mean(rs ** 2)))))
    return rows


def band_null(c, r, fs, mask, lo, hi, order=4):
    sos = butter(order, [lo, hi], btype="bandpass", fs=fs, output="sos")
    cb = sosfiltfilt(sos, c[mask])
    rb = sosfiltfilt(sos, r[mask])
    cd = db(np.sqrt(np.mean(cb ** 2)))
    rd = db(np.sqrt(np.mean(rb ** 2)))
    return cd, rd, rd - cd


def band_table(c, r, fs, mask, bands):
    return [(name,) + band_null(c, r, fs, mask, lo, hi) for name, lo, hi in bands]


def apply_eq(y, eq_path):
    if not eq_path:
        return y
    h = np.load(eq_path)
    return np.stack([fftconvolve(y[:, ch], h, mode="full")[: y.shape[0]] for ch in range(2)], axis=1)


# --------------------------------------------------------- candidate filters

def hp1_lfilter(x, fs, fc):
    """The same first-order high-pass render_sdly already implements
    (y = x - x1 + a*y1, a = exp(-2 pi fc/fs)), vectorized."""
    a = math.exp(-2.0 * math.pi * fc / fs)
    return lfilter([1.0, -1.0], [1.0, -a], x)


def rbj_sos(kind, fs, fc, q):
    w0 = 2.0 * math.pi * fc / fs
    alpha = math.sin(w0) / (2.0 * q)
    cosw0 = math.cos(w0)
    if kind == "hp":
        b0, b1, b2 = (1 + cosw0) / 2, -(1 + cosw0), (1 + cosw0) / 2
    elif kind == "lp":
        b0, b1, b2 = (1 - cosw0) / 2, 1 - cosw0, (1 - cosw0) / 2
    elif kind == "ap":
        b0, b1, b2 = 1 - alpha, -2 * cosw0, 1 + alpha
    else:
        raise ValueError(kind)
    a0, a1, a2 = 1 + alpha, -2 * cosw0, 1 - alpha
    return np.array([[b0 / a0, b1 / a0, b2 / a0, 1.0, a1 / a0, a2 / a0]])


def hshelf_sos(fs, fc, gain_db, q=0.707):
    """RBJ high shelf."""
    A = 10 ** (gain_db / 40.0)
    w0 = 2.0 * math.pi * fc / fs
    cosw0, sinw0 = math.cos(w0), math.sin(w0)
    alpha = sinw0 / 2.0 * math.sqrt((A + 1 / A) * (1 / q - 1) + 2)
    sq = 2 * math.sqrt(A) * alpha
    b0 = A * ((A + 1) + (A - 1) * cosw0 + sq)
    b1 = -2 * A * ((A - 1) + (A + 1) * cosw0)
    b2 = A * ((A + 1) + (A - 1) * cosw0 - sq)
    a0 = (A + 1) - (A - 1) * cosw0 + sq
    a1 = 2 * ((A - 1) - (A + 1) * cosw0)
    a2 = (A + 1) - (A - 1) * cosw0 - sq
    return np.array([[b0 / a0, b1 / a0, b2 / a0, 1.0, a1 / a0, a2 / a0]])


def apply_sos(x, sos):
    return sosfilt(sos, x)


# ---------------------------------------------------- delay-loop re-renders

def _fbgain(spec, coef):
    b = spec.get("blocks", {}).get("feedback_gain", {})
    return Gain(coef, b.get("wordlength"), b.get("rounding", "truncate"), b.get("overflow", "saturate"))


def _delay_line(spec, fs):
    b = spec.get("blocks", {}).get("delay_line", {})
    st = Storage(b.get("storage"))
    max_samples = int(b.get("max_ms", 1000) * fs / 1000.0) + 64
    return DelayLine(max_samples, st, b.get("interp", "none")), st


def _damp(spec, fs, hz):
    b = spec.get("blocks", {}).get("damp", {})
    return OnePoleLP(fs, hz, b.get("wordlength"), b.get("rounding", "truncate"))


def render_sdly_hp2loop(x, spec, fs, fc, q):
    """render_sdly (engine/effects.py) with the single-pole DC-blocker on the
    delay-line write replaced by an RBJ second-order high-pass, still on the
    write path (inside the feedback loop -> compounds per repeat). Mirrors
    render_sdly exactly otherwise. Not used by the plugin; evaluation only."""
    p = spec["params"]
    m = spec["maps"]
    n = x.shape[0]
    y = np.zeros_like(x)
    w0 = 2.0 * math.pi * fc / fs
    alpha = math.sin(w0) / (2.0 * q)
    cosw0 = math.cos(w0)
    b0, b1, b2 = (1 + cosw0) / 2, -(1 + cosw0), (1 + cosw0) / 2
    a0, a1, a2 = 1 + alpha, -2 * cosw0, 1 - alpha
    b0, b1, b2, a1, a2 = b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0
    for ch, side in enumerate(("L", "R")):
        D = apply_map(m["delay"], p[f"{side} Dly"], fs)
        fb = apply_map(m["feedback"], p[f"{side} Fb"], fs)
        damp_hz = apply_map(m["high_damp"], p["High Damp"], fs)
        wet = apply_map(m["balance"], p[f"{side} Bal"], fs)
        dry = apply_map(m["dry"], p[f"{side} Bal"], fs) if "dry" in m else 1.0 - wet
        dl, st = _delay_line(spec, fs)
        lp = _damp(spec, fs, damp_hz)
        g = _fbgain(spec, fb)
        xin = x[:, ch].tolist()
        out = [0.0] * n
        hx1 = hx2 = hy1 = hy2 = 0.0
        for i in range(n):
            xi = xin[i]
            r = dl.read(D)
            w = xi + g(r)
            hy = b0 * w + b1 * hx1 + b2 * hx2 - a1 * hy1 - a2 * hy2
            hx2, hx1 = hx1, w
            hy2, hy1 = hy1, hy
            w = hy
            if w > 0.999969:
                w = 0.999969
            elif w < -1.0:
                w = -1.0
            dl.write(lp(w))
            out[i] = dry * xi + wet * r
        y[:, ch] = out
    return y


def render_wet_only(spec_base, params_override, x, fs_in):
    """render_spec with L Bal/R Bal forced to 50 (wet=1, dry=0) so the output
    is exactly the raw delay-line read r, with the given Fb/Damp/Delay."""
    spec = copy.deepcopy(spec_base)
    spec["params"].update(params_override)
    spec["params"]["L Bal"] = 50
    spec["params"]["R Bal"] = 50
    y, _ = render_spec(spec, x, fs_in)
    return y


def render_converters_only(spec_base, x, fs_in):
    """render_spec with everything nulled to wet=0 -> output is x through the
    resample-up/converter/resample-down round trip alone."""
    spec = copy.deepcopy(spec_base)
    spec["params"].update({"L Fb": 0, "R Fb": 0, "High Damp": 0, "L Bal": 0, "R Bal": 0})
    y, _ = render_spec(spec, x, fs_in)
    return y


# --------------------------------------------------------------------- main

def report_capture(label, cap_path, y_full, x, fs, layout):
    """Given a fully-assembled model output y_full (already eq'd/chain'd),
    return per-channel (seg rows, lf band rows, hf band rows, null_db_total)."""
    cap, _ = load_aligned(cap_path, x, fs, layout)
    out = {"label": label, "channels": []}
    for ch, side in enumerate("LR"):
        c = cap[: len(y_full), ch]
        m = y_full[: len(c), ch]
        lag, g, mask, m2, r = align_gain_residual(c, m, fs, layout)
        segs = seg_table(c, r, fs, layout)
        lf = band_table(c, r, fs, mask, LF_BANDS)
        hf = band_table(c, r, fs, mask, HF_BANDS)
        tot_c = db(np.sqrt(np.mean(c[mask] ** 2)))
        tot_r = db(np.sqrt(np.mean(r[mask] ** 2)))
        out["channels"].append({"side": side, "lag": lag, "gain_db": db(abs(g)),
                                 "seg": segs, "lf": lf, "hf": hf, "null_total": tot_r - tot_c})
    return out


def fmt_band_row(rows):
    return " | ".join(f"{n}: {nd:+.1f} dB" for n, cd, rd, nd in rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "chain-fit-2026-09-13.md"))
    a = ap.parse_args()

    x, fs = load_signal()
    layout = load_layout(LAYOUT_PATH)
    spec_base = load_spec(MODEL_PATH)

    lines = []
    def p(s=""):
        lines.append(s)
        print(s)

    p("# Chain fit, 2026-09-13")
    p()
    p("Generated by `tests/chain_fit.py`. All null-test numbers use the exact")
    p("align + one-gain-fit method in `tests/null_test.py` (imported, not")
    p("reimplemented). LF/HF band numbers are the capture and residual RMS in")
    p("dBFS inside a 4th-order Butterworth bandpass (`scipy.signal.sosfiltfilt`,")
    p("zero-phase, measurement only) over the full non-silence duration excluding")
    p("the ramp -- the same mask `null_test.py` uses for its overall total.")
    p()

    # ---------------------------------------------------------- baseline ("before")
    p("## Before (current state: model as shipped, --eq out/null/chain-eq-bypass.npy)")
    p()
    before = {}
    for label, path in [("dry (Bal 0)", CAP_DRY), ("wet (Bal 50)", CAP_WET), ("Fb 25 (Bal 25)", CAP_FB25)]:
        name = os.path.splitext(os.path.basename(path))[0]
        spec = copy.deepcopy(spec_base)
        spec["params"].update(params_from_name(name))
        y, _ = render_spec(spec, x, fs)
        y = apply_eq(y, EQ_BASELINE)
        rep = report_capture(label, path, y, x, fs, layout)
        before[label] = rep
        p(f"### {label}")
        for chrep in rep["channels"]:
            p(f"- {chrep['side']}: null {chrep['null_total']:+.1f} dB overall")
            p(f"  - LF: {fmt_band_row(chrep['lf'])}")
            p(f"  - HF: {fmt_band_row(chrep['hf'])}")
        p()

    # ============================================================ LF STAGE FIT
    p("## LF stage fit")
    p()
    p("Step 1: fit a single-pass filter against the all-wet, no-feedback capture")
    p("(Bal 50, Fb 0). With Fb 0 the loop never re-injects, so 'inside the loop'")
    p("and 'once at the tap' are mathematically identical (delay commutes with an")
    p("LTI filter) -- this step only pins down the filter's own shape, not its")
    p("placement.")
    p()

    # model output for the wet capture (already computed above, before[wet]),
    # but we need the RAW (pre-eq) wet render to layer eq + candidate LF filter
    # in a controlled order, and the eq's own impulse for later use.
    name_wet = os.path.splitext(os.path.basename(CAP_WET))[0]
    y_wet_raw = render_wet_only(spec_base, params_from_name(name_wet), x, fs)  # == raw r, Fb0
    eq = np.load(EQ_BASELINE)
    cap_wet, _ = load_aligned(CAP_WET, x, fs, layout)

    def score_lf(y_raw_l, fc, q, order):
        """Lower is better: sum of squared linear residual in the 20-640 Hz band,
        summed over L+R, for the wet/Fb0 capture."""
        if order == 1:
            filt = np.stack([hp1_lfilter(y_raw_l[:, 0], fs, fc), hp1_lfilter(y_raw_l[:, 1], fs, fc)], axis=1)
        else:
            sos = rbj_sos("hp", fs, fc, q)
            filt = np.stack([apply_sos(y_raw_l[:, 0], sos), apply_sos(y_raw_l[:, 1], sos)], axis=1)
        y = apply_eq(filt, EQ_BASELINE)
        tot = 0.0
        for ch in range(2):
            c = cap_wet[: len(y), ch]
            m = y[: len(c), ch]
            _, _, mask, _, r = align_gain_residual(c, m, fs, layout)
            sos_lf = butter(4, [20, 640], btype="bandpass", fs=fs, output="sos")
            rb = sosfiltfilt(sos_lf, r[mask])
            tot += float(np.mean(rb ** 2))
        return tot

    grid1 = [3, 4, 5, 6.5, 8, 10, 13, 16, 20, 25, 30, 40]
    res1 = [(fc, score_lf(y_wet_raw, fc, None, 1)) for fc in grid1]
    best1 = min(res1, key=lambda t: t[1])
    p("Order-1 (the model's existing `input_hpf_hz` shape), fc grid, wet/Fb0 capture,")
    p("score = mean-square residual in the 20-640 Hz band (linear, both channels):")
    p()
    p("| fc (Hz) | score (linear, x1e-6) |")
    p("|---|---|")
    for fc, s in res1:
        p(f"| {fc} | {s * 1e6:.3f} |")
    p(f"\nBest order-1: fc = {best1[0]} Hz, score {best1[1]*1e6:.3f}e-6\n")

    grid2_fc = [10, 15, 20, 25, 30, 40, 50, 60, 80]
    grid2_q = [0.3, 0.5, 0.707, 1.0, 1.4, 2.0]
    res2 = [(fc, q, score_lf(y_wet_raw, fc, q, 2)) for fc in grid2_fc for q in grid2_q]
    best2 = min(res2, key=lambda t: t[2])
    p("Order-2 (RBJ high-pass, fc/Q grid), same capture/score:")
    p()
    p("| fc (Hz) \\ Q | " + " | ".join(str(q) for q in grid2_q) + " |")
    p("|---" * (len(grid2_q) + 1) + "|")
    for fc in grid2_fc:
        row = [f"{s*1e6:.3f}" for (fc2, q2, s) in res2 if fc2 == fc]
        p(f"| {fc} | " + " | ".join(row) + " |")
    p(f"\nBest order-2: fc = {best2[0]} Hz, Q = {best2[1]}, score {best2[2]*1e6:.3f}e-6\n")

    lf_order = 1 if best1[1] <= best2[2] else 2
    lf_fc, lf_q = (best1[0], None) if lf_order == 1 else (best2[0], best2[1])
    p(f"Winner by score: order {lf_order}, fc {lf_fc} Hz" + (f", Q {lf_q}" if lf_q else "") + ".")
    p()

    def apply_lf_hp(y2):
        if lf_order == 1:
            return np.stack([hp1_lfilter(y2[:, 0], fs, lf_fc), hp1_lfilter(y2[:, 1], fs, lf_fc)], axis=1)
        sos = rbj_sos("hp", fs, lf_fc, lf_q)
        return np.stack([apply_sos(y2[:, 0], sos), apply_sos(y2[:, 1], sos)], axis=1)

    # per-band detail for the winner on the wet/Fb0 capture
    filt = apply_lf_hp(y_wet_raw)
    y_wet_winner = apply_eq(filt, EQ_BASELINE)
    rep_wet_lf = report_capture("wet (Bal 50), LF winner", CAP_WET, y_wet_winner, x, fs, layout)
    p("LF winner applied to the wet/Fb0 capture (single-pass; placement not yet decided):")
    for chrep in rep_wet_lf["channels"]:
        p(f"- {chrep['side']}: null {chrep['null_total']:+.1f} dB overall, LF: {fmt_band_row(chrep['lf'])}")
    p()
    p("The 80-640 Hz bands got *worse* under a pure high-pass -- the docs already")
    p("flag this as a phase problem ('the input coupling high-pass's phase, not")
    p("modelled yet', null_test 2026-09-13 entry), not a magnitude one, so a")
    p("second stage that changes phase without touching magnitude (an RBJ")
    p("allpass, in series after the winning high-pass) is tried next, scored on")
    p("the same wet/Fb0 capture and the same 20-640 Hz combined metric.")
    p()

    def score_lf_ap(fc, q):
        filt2 = apply_sos(apply_lf_hp(y_wet_raw)[:, 0], rbj_sos("ap", fs, fc, q))
        filt2r = apply_sos(apply_lf_hp(y_wet_raw)[:, 1], rbj_sos("ap", fs, fc, q))
        y = apply_eq(np.stack([filt2, filt2r], axis=1), EQ_BASELINE)
        tot = 0.0
        for ch in range(2):
            c = cap_wet[: len(y), ch]
            m = y[: len(c), ch]
            _, _, mask, _, r = align_gain_residual(c, m, fs, layout)
            sos_lf = butter(4, [20, 640], btype="bandpass", fs=fs, output="sos")
            rb = sosfiltfilt(sos_lf, r[mask])
            tot += float(np.mean(rb ** 2))
        return tot

    base_lf_score = score_lf(y_wet_raw, lf_fc, lf_q, lf_order)
    grid_ap_fc = [60, 80, 120, 160, 200, 250, 300, 400, 500]
    grid_ap_q = [0.3, 0.5, 0.707, 1.0, 1.4, 2.0, 3.0]
    res_lfap = [(fc, q, score_lf_ap(fc, q)) for fc in grid_ap_fc for q in grid_ap_q]
    best_lfap = min(res_lfap, key=lambda t: t[2])
    p(f"Baseline (high-pass alone) score: {base_lf_score*1e6:.3f}e-6. Allpass-in-series grid:")
    p()
    p("| fc (Hz) \\ Q | " + " | ".join(str(q) for q in grid_ap_q) + " |")
    p("|---" * (len(grid_ap_q) + 1) + "|")
    for fc in grid_ap_fc:
        row = [f"{s*1e6:.3f}" for (fc2, q2, s) in res_lfap if fc2 == fc]
        p(f"| {fc} | " + " | ".join(row) + " |")
    p(f"\nBest allpass: fc {best_lfap[0]} Hz, Q {best_lfap[1]}, score {best_lfap[2]*1e6:.3f}e-6\n")

    use_lf_ap = best_lfap[2] < base_lf_score * 0.95
    lf_ap_fc, lf_ap_q = (best_lfap[0], best_lfap[1]) if use_lf_ap else (None, None)
    if use_lf_ap:
        p(f"Allpass adopted: fc {lf_ap_fc} Hz, Q {lf_ap_q} (in series after the high-pass).")
    else:
        p("Allpass does not meaningfully help here (< 5% score reduction) -- not adopted;")
        p("the LF stage stays the plain high-pass alone. The 80-640 Hz shortfall is")
        p("reported as an open, unfixed part of the LF signature below.")
    p()

    def apply_lf_full(y2):
        y2 = apply_lf_hp(y2)
        if use_lf_ap:
            sos = rbj_sos("ap", fs, lf_ap_fc, lf_ap_q)
            y2 = np.stack([apply_sos(y2[:, 0], sos), apply_sos(y2[:, 1], sos)], axis=1)
        return y2

    y_wet_winner2 = apply_eq(apply_lf_full(y_wet_raw), EQ_BASELINE)
    rep_wet_lf2 = report_capture("wet (Bal 50), LF winner + allpass", CAP_WET, y_wet_winner2, x, fs, layout)
    p("Full LF stage (high-pass" + (" + allpass" if use_lf_ap else "") + ") applied to the wet/Fb0 capture:")
    for chrep in rep_wet_lf2["channels"]:
        p(f"- {chrep['side']}: null {chrep['null_total']:+.1f} dB overall, LF: {fmt_band_row(chrep['lf'])}")
    p()

    # ------------------------------------------------- Step 2: placement (Fb25)
    p("### Placement: inside vs outside the feedback loop (Feedback 25 capture)")
    p()
    p("With feedback present, 'inside the loop' (filter on the delay-line write,")
    p("so repeat j carries j filter passes) and 'at the tap' (filter applied once")
    p("to the fully-summed wet read, not compounding) are genuinely different")
    p("systems. Both are built from the winning fc/Q above and compared against")
    p("the Feedback 25 / Bal 25 capture.")
    p()

    name_fb25 = os.path.splitext(os.path.basename(CAP_FB25))[0]
    params_fb25 = params_from_name(name_fb25)
    bal_fb25 = params_fb25.get("L Bal", 25)
    wet_c = apply_map(spec_base["maps"]["balance"], bal_fb25, fs)
    dry_c = apply_map(spec_base["maps"]["dry"], bal_fb25, fs)

    y_r_fb25 = render_wet_only(spec_base, params_fb25, x, fs)          # raw r at Fb 25, no chain filter
    y_conv = render_converters_only(spec_base, x, fs)                   # dry component (converters only)

    # tap placement: filter r once (full LF stage: hp [+ allpass]), then remix
    # with the real Balance weights
    r_tap = apply_lf_full(y_r_fb25)
    y_tap = dry_c * y_conv + wet_c * r_tap
    y_tap = apply_eq(y_tap, EQ_BASELINE)
    rep_tap = report_capture("Fb25, LF at tap (non-compounding)", CAP_FB25, y_tap, x, fs, layout)

    # loop placement: real per-sample re-render with the filter inside the loop
    spec_loop = copy.deepcopy(spec_base)
    spec_loop["params"].update(params_fb25)
    if lf_order == 1:
        spec_loop["blocks"]["delay_line"]["input_hpf_hz"] = lf_fc
        y_loop, _ = render_spec(spec_loop, x, fs)
    else:
        xup = None  # render_sdly_hp2loop needs device-rate input; reuse render_spec's own resampling by hand
        from fractions import Fraction
        from scipy.signal import resample_poly
        fs_dev = float(spec_loop.get("sample_rate", 39062.5))
        fr = Fraction(fs_dev / fs).limit_denominator(4096)
        up, down = fr.numerator, fr.denominator
        win = spec_loop.get("resampler", {}).get("window", ("kaiser", 14.0))
        xin2 = np.stack([x, x], axis=1)
        xd = resample_poly(xin2, up, down, axis=0, window=win)
        scale = float(1 << (spec_loop.get("converters", {}).get("bits", 18) - 1))
        xd = np.clip(np.round(xd * scale), -scale, scale - 1) / scale
        yd = render_sdly_hp2loop(xd, spec_loop, fs_dev, lf_fc, lf_q)
        yd = np.clip(np.round(yd * scale), -scale, scale - 1) / scale
        y_loop = resample_poly(yd, down, up, axis=0, window=win)[: xin2.shape[0]]
    if use_lf_ap:
        # the allpass (if adopted) is scored/placed once at the tap regardless
        # of where the high-pass sits -- only the high-pass's compounding is
        # under test here
        sos = rbj_sos("ap", fs, lf_ap_fc, lf_ap_q)
        y_loop = np.stack([apply_sos(y_loop[:, 0], sos), apply_sos(y_loop[:, 1], sos)], axis=1)
    y_loop = apply_eq(y_loop, EQ_BASELINE)
    rep_loop = report_capture("Fb25, LF inside the loop (compounding)", CAP_FB25, y_loop, x, fs, layout)

    p(f"High-pass under test: order {lf_order}, fc {lf_fc} Hz" + (f", Q {lf_q}" if lf_q else "") + "."
      + (f" Allpass (fc {lf_ap_fc} Hz, Q {lf_ap_q}) applied once at the tap in both rows below." if use_lf_ap else ""))
    p()
    p("| placement | side | null overall | LF 20-40 | LF 40-80 | LF 80-160 | LF 160-320 | LF 320-640 |")
    p("|---|---|---|---|---|---|---|---|")
    for label, rep in [("tap (non-compounding)", rep_tap), ("loop (compounding)", rep_loop)]:
        for chrep in rep["channels"]:
            vals = " | ".join(f"{nd:+.1f}" for _, _, _, nd in chrep["lf"])
            p(f"| {label} | {chrep['side']} | {chrep['null_total']:+.1f} | {vals} |")
    p()
    tap_score = sum(nd for chrep in rep_tap["channels"] for _, _, _, nd in chrep["lf"])
    loop_score = sum(nd for chrep in rep_loop["channels"] for _, _, _, nd in chrep["lf"])
    placement = "loop (compounding)" if loop_score < tap_score else "tap (non-compounding)"
    p(f"Sum of LF-band null_db (more negative = better): tap {tap_score:+.1f} dB, "
      f"loop {loop_score:+.1f} dB. Evidence favors: **{placement}**.")
    p()

    # ---------------------------------------------------------------- HF STAGE
    p("## HF stage fit")
    p()
    p("`out/null/chain-eq-bypass.npy` (the existing dry-chain FIR, derived by")
    p("`chain_eq.py --passthrough --f-hi 19400` from the Bypass capture) tapers")
    p("to -54 dB by 19 kHz and shows a +2.9 dB bump at 18 kHz -- regularization")
    p("artefacts of the spectral-division extraction, not the converters' real")
    p("response (service manual: +-1 dB to 19.2 kHz, +-3 dB to 19.5 kHz). Any")
    p("filter chained after that FIR cannot recover energy it has already")
    p("discarded, so the HF fit here first replaces the FIR's response above")
    p("~11 kHz with a flat (0 dB, no added phase) continuation, then fits a short")
    p("IIR section in series to supply the real 12-19.5 kHz correction, scored")
    p("directly against the null-test residual (not by spectral division).")
    p()

    N = 1 << 16
    Hold = np.fft.rfft(eq, N)
    freqs = np.fft.rfftfreq(N, 1.0 / fs)
    lo_x, hi_x = 10000.0, 12000.0
    L = np.ones_like(freqs)
    L[freqs > hi_x] = 0.0
    edge = (freqs >= lo_x) & (freqs <= hi_x)
    L[edge] = 0.5 + 0.5 * np.cos(np.pi * (freqs[edge] - lo_x) / (hi_x - lo_x))
    # Magnitude-only correction: hold |H| flat at its value at hi_x beyond that
    # point, blended in over lo_x..hi_x, but leave the PHASE untouched
    # everywhere. An earlier version blended the complex spectrum toward the
    # constant 1+0j, which also overwrote phase with 0 right where h_old's own
    # phase is worst-behaved (regularization artefact, see the printed table
    # above -- it swings +-180 deg over a few hundred Hz there); that
    # discontinuity is non-local in time (Fourier duality) and it broke
    # cancellation broadband, not just above 12 kHz (verified: null_total on
    # every capture collapsed by ~10 dB with that version, not just the HF
    # bands -- a regression, discarded). Magnitude-only clamping avoids the
    # phase discontinuity and is checked below to leave <10 kHz unaffected.
    Mold = np.abs(Hold)
    Pold = np.angle(Hold)
    m_hold = float(np.interp(hi_x, freqs, Mold))
    Mtarget = np.where(freqs > hi_x, m_hold, Mold)
    Mnew = Mold * L + Mtarget * (1.0 - L)
    Hlf = Mnew * np.exp(1j * Pold)
    eq_lf = np.fft.irfft(Hlf, N)[: len(eq)]
    e3 = int(0.001 * fs)
    ramp3 = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, e3))
    eq_lf[-e3:] *= ramp3[::-1]

    # verify the crossover is sane
    Hcheck = np.fft.rfft(eq_lf, N)
    p("Sanity check of the crossover-modified FIR (`eq_lf`, dB re 1 kHz):")
    row = " ".join(f"{f0}:{20*np.log10(abs(Hcheck[np.argmin(np.abs(freqs-f0))])/abs(Hcheck[np.argmin(np.abs(freqs-1000))])):+.2f}"
                    for f0 in [1000, 8000, 10000, 11000, 12000, 14000, 16000, 18000, 19000, 19500])
    p(f"  {row}")
    p()
    rep_bypass_orig = report_capture("Bypass, original eq", CAP_BYPASS, apply_eq(y_conv, EQ_BASELINE), x, fs, layout)
    y_eqlf_only = np.stack([fftconvolve(y_conv[:, ch], eq_lf, mode="full")[: y_conv.shape[0]] for ch in range(2)], axis=1)
    rep_bypass_eqlf_only = report_capture("Bypass, eq_lf only (no HF stage)", CAP_BYPASS, y_eqlf_only, x, fs, layout)
    p("Regression check -- eq_lf ALONE (no HF stage yet) vs the original eq, on the Bypass capture")
    p("(this must NOT collapse the overall null; the earlier complex-blend version did, by ~10 dB):")
    p()
    p("| eq | side | null overall |")
    p("|---|---|---|")
    for label, rep in [("original (chain-eq-bypass.npy)", rep_bypass_orig), ("eq_lf (magnitude-clamped >11 kHz)", rep_bypass_eqlf_only)]:
        for chrep in rep["channels"]:
            p(f"| {label} | {chrep['side']} | {chrep['null_total']:+.1f} |")
    p()

    cap_bypass, _ = load_aligned(CAP_BYPASS, x, fs, layout)
    y_conv_raw = y_conv  # converters-only render, no eq yet

    def score_hf(y_raw, eq_fir, hf_sos_list, cap):
        y = apply_eq(y_raw, None)
        y_e = np.stack([fftconvolve(y[:, ch], eq_fir, mode="full")[: y.shape[0]] for ch in range(2)], axis=1)
        if hf_sos_list:
            for sos in hf_sos_list:
                y_e = np.stack([apply_sos(y_e[:, 0], sos), apply_sos(y_e[:, 1], sos)], axis=1)
        tot = 0.0
        for ch in range(2):
            c = cap[: len(y_e), ch]
            m = y_e[: len(c), ch]
            _, _, mask, _, r = align_gain_residual(c, m, fs, layout)
            sos_hf = butter(4, [12000, 19500], btype="bandpass", fs=fs, output="sos")
            rb = sosfiltfilt(sos_hf, r[mask])
            tot += float(np.mean(rb ** 2))
        return tot

    base_hf_score = score_hf(y_conv_raw, eq_lf, None, cap_bypass)
    p(f"Baseline (eq_lf only, no HF stage) 12-19.5 kHz score: {base_hf_score*1e6:.3f}e-6\n")

    grid_ap_fc = [15000, 16000, 17000, 18000, 18500, 19000, 19200]
    grid_ap_q = [0.5, 0.7, 1.0, 1.4, 2.0, 3.0]
    res_ap = [(fc, q, score_hf(y_conv_raw, eq_lf, [rbj_sos("ap", fs, fc, q)], cap_bypass))
              for fc in grid_ap_fc for q in grid_ap_q]
    best_ap = min(res_ap, key=lambda t: t[2])
    p("Allpass-only candidates (RBJ 2nd-order allpass, phase correction, magnitude untouched):")
    p()
    p("| fc (Hz) \\ Q | " + " | ".join(str(q) for q in grid_ap_q) + " |")
    p("|---" * (len(grid_ap_q) + 1) + "|")
    for fc in grid_ap_fc:
        row = [f"{s*1e6:.3f}" for (fc2, q2, s) in res_ap if fc2 == fc]
        p(f"| {fc} | " + " | ".join(row) + " |")
    p(f"\nBest allpass: fc {best_ap[0]} Hz, Q {best_ap[1]}, score {best_ap[2]*1e6:.3f}e-6\n")

    grid_sh_fc = [12000, 14000, 16000, 18000]
    grid_sh_db = [-3, -1.5, 1.5, 3, 4.5]
    res_sh = [(fc, gdb, score_hf(y_conv_raw, eq_lf, [rbj_sos("ap", fs, best_ap[0], best_ap[1]), hshelf_sos(fs, fc, gdb)], cap_bypass))
              for fc in grid_sh_fc for gdb in grid_sh_db]
    best_sh = min(res_sh, key=lambda t: t[2])
    p("Allpass (fixed at the winner above) + high-shelf magnitude trim, in series:")
    p()
    p("| shelf fc (Hz) \\ gain (dB) | " + " | ".join(str(g) for g in grid_sh_db) + " |")
    p("|---" * (len(grid_sh_db) + 1) + "|")
    for fc in grid_sh_fc:
        row = [f"{s*1e6:.3f}" for (fc2, g2, s) in res_sh if fc2 == fc]
        p(f"| {fc} | " + " | ".join(row) + " |")
    p(f"\nBest allpass+shelf: shelf fc {best_sh[0]} Hz, gain {best_sh[1]} dB, score {best_sh[2]*1e6:.3f}e-6\n")

    use_shelf = best_sh[2] < best_ap[2] * 0.95  # only keep the shelf if it meaningfully helps
    hf_sos = [rbj_sos("ap", fs, best_ap[0], best_ap[1])]
    hf_desc = f"2nd-order allpass, fc {best_ap[0]} Hz, Q {best_ap[1]}"
    if use_shelf:
        hf_sos.append(hshelf_sos(fs, best_sh[0], best_sh[1]))
        hf_desc += f" + high shelf, fc {best_sh[0]} Hz, {best_sh[1]:+d} dB"
    p(f"HF stage adopted: {hf_desc}.")
    p()

    def full_chain(y_raw):
        y = np.stack([fftconvolve(y_raw[:, ch], eq_lf, mode="full")[: y_raw.shape[0]] for ch in range(2)], axis=1)
        for sos in hf_sos:
            y = np.stack([apply_sos(y[:, 0], sos), apply_sos(y[:, 1], sos)], axis=1)
        return y

    rep_bypass_before = report_capture("Bypass, before", CAP_BYPASS,
                                        apply_eq(y_conv_raw, EQ_BASELINE), x, fs, layout)
    rep_bypass_after = report_capture("Bypass, after", CAP_BYPASS, full_chain(y_conv_raw), x, fs, layout)
    p("Bypass capture, HF bands, before vs after:")
    p()
    p("| | side | null | 12k-16k | 16k-18k | 18k-19.5k |")
    p("|---|---|---|---|---|---|")
    for label, rep in [("before", rep_bypass_before), ("after", rep_bypass_after)]:
        for chrep in rep["channels"]:
            vals = " | ".join(f"{nd:+.1f}" for _, _, _, nd in chrep["hf"])
            p(f"| {label} | {chrep['side']} | {chrep['null_total']:+.1f} | {vals} |")
    p()

    # ================================================== FULL BEFORE/AFTER TABLE
    p("## Full before/after, all three captures")
    p()
    p("Chain adopted for 'after': " + f"LF = order {lf_order} high-pass, fc {lf_fc} Hz"
      + (f", Q {lf_q}" if lf_q else "") + f", placement = {placement}; "
      + f"HF = {hf_desc}, applied after a crossover-flattened version of the existing "
      + "chain-eq-bypass.npy (flat/no correction above ~12 kHz instead of its own taper).")
    p()

    def build_after(params, force_bal=None):
        """Full model render with the adopted LF (correct placement) + eq_lf + HF stage."""
        if placement.startswith("loop") and lf_order == 1:
            spec = copy.deepcopy(spec_base)
            spec["params"].update(params)
            spec["blocks"]["delay_line"]["input_hpf_hz"] = lf_fc
            y_raw, _ = render_spec(spec, x, fs)
        elif placement.startswith("loop") and lf_order == 2:
            from fractions import Fraction
            from scipy.signal import resample_poly
            spec = copy.deepcopy(spec_base)
            spec["params"].update(params)
            fs_dev = float(spec.get("sample_rate", 39062.5))
            fr = Fraction(fs_dev / fs).limit_denominator(4096)
            up, down = fr.numerator, fr.denominator
            win = spec.get("resampler", {}).get("window", ("kaiser", 14.0))
            xin2 = np.stack([x, x], axis=1)
            xd = resample_poly(xin2, up, down, axis=0, window=win)
            scale = float(1 << (spec.get("converters", {}).get("bits", 18) - 1))
            xd = np.clip(np.round(xd * scale), -scale, scale - 1) / scale
            yd = render_sdly_hp2loop(xd, spec, fs_dev, lf_fc, lf_q)
            yd = np.clip(np.round(yd * scale), -scale, scale - 1) / scale
            y_raw = resample_poly(yd, down, up, axis=0, window=win)[: xin2.shape[0]]
        else:
            y_raw = None
        if placement.startswith("loop"):
            if use_lf_ap:
                sos = rbj_sos("ap", fs, lf_ap_fc, lf_ap_q)
                y_raw = np.stack([apply_sos(y_raw[:, 0], sos), apply_sos(y_raw[:, 1], sos)], axis=1)
        else:
            wet_p = apply_map(spec_base["maps"]["balance"], params.get("L Bal", 25), fs)
            dry_p = apply_map(spec_base["maps"]["dry"], params.get("L Bal", 25), fs)
            r_raw = render_wet_only(spec_base, params, x, fs)
            conv = render_converters_only(spec_base, x, fs)
            rf = apply_lf_full(r_raw)
            y_raw = dry_p * conv + wet_p * rf
        return full_chain(y_raw)

    after = {}
    for label, path in [("dry (Bal 0)", CAP_DRY), ("wet (Bal 50)", CAP_WET), ("Fb 25 (Bal 25)", CAP_FB25)]:
        name = os.path.splitext(os.path.basename(path))[0]
        params = params_from_name(name)
        y = build_after(params)
        rep = report_capture(label, path, y, x, fs, layout)
        after[label] = rep
        p(f"### {label}")
        p()
        p("BEFORE:")
        for chrep in before[label]["channels"]:
            p(f"- {chrep['side']}: null {chrep['null_total']:+.1f} dB  |  LF: {fmt_band_row(chrep['lf'])}  |  HF: {fmt_band_row(chrep['hf'])}")
        p("AFTER:")
        for chrep in rep["channels"]:
            p(f"- {chrep['side']}: null {chrep['null_total']:+.1f} dB  |  LF: {fmt_band_row(chrep['lf'])}  |  HF: {fmt_band_row(chrep['hf'])}")
        p()
        p("Per-segment (AFTER), broadband dB:")
        p()
        p("| segment | capture dBFS | residual dBFS | null dB |")
        p("|---|---|---|---|")
        for chrep in rep["channels"]:
            p(f"_{chrep['side']}_")
            for n_, cd, rd in chrep["seg"]:
                p(f"| {n_} | {cd:.1f} | {rd:.1f} | {rd-cd:+.1f} |")
        p()

    p("## Files created or changed")
    p()
    p("- `tests/chain_fit.py` (new)")
    p(f"- `{os.path.relpath(a.out, ROOT)}` (new, this file)")
    p()
    p("No files under `dsp/`, `plugin/`, `models/` or the project notes were")
    p("touched. `models/ax30g-sdly.json` was not modified;")
    p("the LF/HF stages above are a proposal for `blocks`/`maps` additions, not")
    p("yet applied to the model.")

    with open(a.out, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("\nwrote", a.out)


if __name__ == "__main__":
    main()
