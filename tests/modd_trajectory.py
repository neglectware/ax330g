#!/usr/bin/env python3
"""Modulated-delay acceptance test: compare the DELAY TRAJECTORY, not a
waveform null.

Rationale (project lead, 2026-09-13): the real unit's LFO free-runs from
power-on, so its phase at the moment a capture starts is not reproducible.
A waveform null is therefore meaningless for a modulated delay in normal
use -- what the ear actually hears is how the delay time moves over time.
This script extracts that movement (in device-rate samples) from both the
real capture and the model, fits the model's free LFO phase (and a small
constant offset) to the capture, and reports how far the two trajectories
disagree -- numbers only, never a claim of a "match".

    modd_trajectory.py CAPTURE.wav
        [--model models/ax30g-modd.json]
        [--signal capture/signalset-lfo.wav]
        [--layout capture/layout-lfo.json]
        [--force-shape sin]   # override blocks.lfo.shape in memory (no file edit)

Method (identical to docs/modd-lfo-table-2026-09-13.md's LFO-waveform
extraction): capture and model L channel over the sine20 segment, 0.5 s
trimmed off each end (per this task's spec) then a further 0.05 s trimmed
after filtering (filter-edge transients, matching analysis.lfo.analyze_lfo);
4th-order Butterworth bandpass 700-1300 Hz (analysis.lfo.pitch_track,
f0=1000, bw=300); analytic-signal (Hilbert) instantaneous frequency;
dev = 1 - f/f0 (the delay's derivative); cumulative-sum integrated to a
delay trajectory; linear drift removed by least-squares fit and subtraction;
scaled from seconds to the model spec's own sample_rate (device-rate
samples).

The model's LFO phase at t=0 (blocks.lfo.phase) is not a device law -- it is
fit per capture, by folding both trajectories to one-cycle templates
(256-bin synchronous average at the model's own fitted period, the same
machinery used for the period fit) and circularly cross-correlating those
two templates -- an exact circular correlation over one LFO period, at the
cost of a 256-point FFT rather than a shift search over the full ~1.4 M
-sample trajectory. The sign of the phase<->time-shift relationship is
derived from engine/blocks.py's LFO (phase(t) = phase0 + rate*t, so a
phase0 of "d" is exactly a time-shift of d/rate seconds) and then checked
empirically: both the derived phase and its period-complement are actually
rendered and the lower-RMS one is kept, so a sign slip cannot silently
corrupt the report.
"""
import argparse
import copy
import os
import sys

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

from analysis.util import load_layout, seg, cut     # noqa: E402
from analysis.run import load_aligned                # noqa: E402
from analysis.lfo import pitch_track                 # noqa: E402
from engine.render import load_spec, render_spec      # noqa: E402
from tests.null_test import params_from_name, frac_shift, best_shift  # noqa: E402

F0 = 1000.0          # tone under test
BW = 300.0           # -> bandpass 700-1300 Hz, matching docs/modd-lfo-table-2026-09-13.md
SEG_TRIM_S = 0.5     # this task's spec: skip 0.5 s at each end of sine20
EDGE_TRIM_S = 0.05   # further filter-edge trim, matching analyze_lfo
WIN_S = 5.0          # start/middle/end report windows



# ------------------------------------------------------------- dry removal

def zero_wet(spec):
    """A copy of the spec with the wet mix forced to zero, so a render gives
    the model's DRY path alone. Works for any model whose wet gain comes from
    maps.wet (CHO) or maps.balance (SDLY, SMOD) with the dry gain in maps.dry.
    NOT valid for MODD, whose render_modd derives dry = 1 - wet internally, so
    zeroing the wet map also moves its dry gain -- except at Balance 50, where
    the dry gain is 0 anyway and the whole output is wet."""
    sp = copy.deepcopy(spec)
    key = "wet" if "wet" in sp["maps"] else ("balance" if "balance" in sp["maps"] else None)
    if key is None:
        raise KeyError("no maps.wet or maps.balance to zero")
    sp["maps"][key] = {"type": "constant", "value": 0.0}
    return sp


def dry_only_mask(layout, fs, n, win_ms):
    """Samples where only the dry path can be present: the first win_ms after
    each click and each burst onset, which for any effect whose shortest delay
    exceeds win_ms contains no wet signal at all."""
    m = np.zeros(n, bool)
    w = int(round(win_ms / 1000.0 * fs))
    for name, key in (("clicks", "click_times"), ("bursts", "burst_times")):
        try:
            s = seg(layout, name)
        except KeyError:
            continue
        for t in s.get(key, []):
            a = int(round(t * fs))
            m[max(a, 0):min(a + w, n)] = True
    return m


def fit_dry(cap_ch, model_dry, layout, fs, win_ms, span=1.0):
    """Gain and fractional lag of the model's dry path against the capture,
    fitted only where the wet path is silent. Returns (gain, lag, null_db).

    The starting lag comes from a cross-correlation over the CLICKS segment,
    not from the dry-only mask: the mask is dominated by the 1 kHz bursts,
    and those alone admit a half-period alias (-11.7 samples at 48 kHz with
    an inverted gain) that a local search will happily walk into. The clicks
    are broadband and aperiodic, so their correlation peak is unambiguous."""
    n = min(len(cap_ch), len(model_dry))
    c, d = cap_ch[:n], model_dry[:n]
    mask = dry_only_mask(layout, fs, n, win_ms)
    if not mask.any():
        raise ValueError("no dry-only windows in this layout")
    try:
        s = seg(layout, "clicks")
        a, b = int(s["start"] * fs), int(s["end"] * fs)
        lag0 = best_shift(c[a:b], d[a:b], fs, max_ms=5.0)
    except KeyError:
        lag0 = 0.0

    def cost(lag):
        m2 = frac_shift(d, lag)
        g = float(np.dot(c[mask], m2[mask]) / max(np.dot(m2[mask], m2[mask]), 1e-20))
        r = c[mask] - g * m2[mask]
        return float(np.dot(r, r)), g

    # golden section on lag0 +- span, bounded so it cannot leave the basin
    lo, hi = lag0 - span, lag0 + span
    gr = (np.sqrt(5.0) - 1.0) / 2.0
    c1, c2 = hi - gr * (hi - lo), lo + gr * (hi - lo)
    f1, f2 = cost(c1)[0], cost(c2)[0]
    for _ in range(22):
        if f1 < f2:
            hi, c2, f2 = c2, c1, f1
            c1 = hi - gr * (hi - lo)
            f1 = cost(c1)[0]
        else:
            lo, c1, f1 = c1, c2, f2
            c2 = lo + gr * (hi - lo)
            f2 = cost(c2)[0]
    lag = 0.5 * (lo + hi)
    e, g = cost(lag)
    cap_e = float(np.dot(c[mask], c[mask]))
    return g, lag, 10 * np.log10(max(e, 1e-30) / max(cap_e, 1e-30))


# ---------------------------------------------------------------- trajectory

def extract_trajectory(ch, fs, t0, t1):
    """Delay trajectory (seconds, drift-removed) of one channel over [t0,t1]."""
    w = cut(ch, fs, t0, t1)
    f = pitch_track(w, fs, f0=F0, bw=BW, smooth_ms=0.3)
    dev = 1.0 - f / F0
    n = int(round(EDGE_TRIM_S * fs))
    dev = dev[n:-n]
    t = np.arange(len(dev)) / fs
    d = np.cumsum(dev) / fs                 # seconds
    p = np.polyfit(t, d, 1)
    d = d - np.polyval(p, t)                # linear drift removed
    return t, d, float(p[0])                # p[0]: removed slope, s/s (diagnostic only)


def synchronous_average(t, d, period_s, n_bins=256):
    phase = (t / period_s) % 1.0
    bins = np.clip((phase * n_bins).astype(int), 0, n_bins - 1)
    sums = np.bincount(bins, weights=d, minlength=n_bins)
    counts = np.bincount(bins, minlength=n_bins)
    return sums / np.maximum(counts, 1)


def fft_rate_guess(t, d):
    n = len(d)
    nfft = 1 << (int(np.ceil(np.log2(n))) + 3)
    fs_eff = 1.0 / (t[1] - t[0])
    D = np.abs(np.fft.rfft(d * np.hanning(n), nfft))
    fr = np.fft.rfftfreq(nfft, 1.0 / fs_eff)
    m = np.where((fr >= 0.05) & (fr <= 25.0))[0]
    k = m[np.argmax(D[m])]
    return float(fr[k])


def fit_period(t, d, f_guess, n_bins=256):
    """Coarse (+-0.02 Hz, 0.00005 Hz step) then fine (+-0.0006 Hz, 0.000004 Hz
    step) grid search maximizing the p-p of the synchronous average -- the
    same method docs/modd-lfo-table-2026-09-13.md used to find the LFO rate."""
    def score(freq):
        avg = synchronous_average(t, d, 1.0 / freq, n_bins)
        return avg.max() - avg.min()

    def grid(fc, half, step):
        freqs = np.arange(fc - half, fc + half + step / 2, step)
        freqs = freqs[freqs > 0]
        scores = [score(fr) for fr in freqs]
        return float(freqs[int(np.argmax(scores))])

    f1 = grid(f_guess, 0.02, 0.00005)
    f2 = grid(f1, 0.0006, 0.000004)
    return f2, 1.0 / f2


def circular_template_fit(cap_template, model_template):
    """Circular cross-correlation of two one-cycle (n_bins-point) templates.
    Returns the fraction of a cycle (0-1) that model_template must be
    delayed by to best match cap_template, and the peak correlation."""
    n = len(cap_template)
    A = np.fft.rfft(cap_template)
    B = np.fft.rfft(model_template)
    corr = np.fft.irfft(A * np.conj(B), n)
    k = int(np.argmax(corr))
    km1, kp1 = (k - 1) % n, (k + 1) % n
    y0, y1, y2 = corr[km1], corr[k], corr[kp1]
    den = y0 - 2 * y1 + y2
    frac = 0.5 * (y0 - y2) / den if den != 0 else 0.0
    shift_cycles = ((k + frac) / n) % 1.0
    return shift_cycles, float(corr[k])


def db(v):
    return 20 * np.log10(max(float(abs(v)), 1e-12))


def rms(x):
    return float(np.sqrt(np.mean(np.asarray(x) ** 2)))


def window_slice(n, fs, t0, length):
    a = int(round(t0 * fs))
    b = a + int(round(length * fs))
    a = max(a, 0)
    b = min(b, n)
    return a, b


# --------------------------------------------------------------- main logic

def render_trajectory(spec, x_signal, fs_sig, t0, t1, device_fs, phase=None, dry=None):
    """dry: the model's dry-path render, subtracted before the pitch track so
    a mixed dry+wet effect (a chorus) is tracked on its wet path alone."""
    sp = copy.deepcopy(spec)
    if phase is not None:
        sp.setdefault("blocks", {}).setdefault("lfo", {})["phase"] = float(phase) % 1.0
    y, _truth = render_spec(sp, x_signal, fs_sig)
    ch = y[:, 0]
    if dry is not None:
        n = min(len(ch), len(dry))
        ch = ch[:n] - dry[:n]
    t, d_s, slope = extract_trajectory(ch, fs_sig, t0, t1)
    return t, d_s * device_fs, slope  # device-rate samples


def analyze(capture_path, model_path, signal_path, layout_path, force_shape=None,
            subtract_dry=False, dry_window_ms=20.0):
    x, fs_sig = sf.read(signal_path, dtype="float64", always_2d=True)
    x = x[:, 0]
    layout = load_layout(layout_path)
    cap, ainfo = load_aligned(capture_path, x, fs_sig, layout)

    spec = load_spec(model_path)
    name = os.path.splitext(os.path.basename(capture_path))[0]
    spec["params"].update(params_from_name(name))
    shape_note = spec["blocks"]["lfo"]["shape"]
    if force_shape:
        spec["blocks"]["lfo"]["shape"] = force_shape
        shape_note = force_shape
    device_fs = float(spec.get("sample_rate", 39057.3))

    s = seg(layout, "sine20")
    t0, t1 = s["start"] + SEG_TRIM_S, s["end"] - SEG_TRIM_S

    # optional dry removal: a chorus mixes dry and wet, and a pitch track of
    # the sum follows the beating between them, not the delay (the bench app's
    # 6-24 Hz readings for a 1 Hz Chorus setting are exactly this). Render the
    # model's dry path once, fit its gain and lag to the capture where only
    # dry can be present, and subtract it from both sides.
    dry_ref = None
    dry_info = None
    cap_ch = cap[:, 0]
    if subtract_dry:
        mdry, _ = render_spec(zero_wet(spec), x, fs_sig, spec_path=os.path.abspath(model_path))
        dry_ref = mdry[:, 0]
        g, lag, nulldb = fit_dry(cap_ch, dry_ref, layout, fs_sig, dry_window_ms)
        n_ = min(len(cap_ch), len(dry_ref))
        cap_ch = cap_ch[:n_] - g * frac_shift(dry_ref[:n_], lag)
        dry_info = {"gain": g, "gain_db": db(g), "lag_samples": lag,
                    "dry_window_ms": dry_window_ms, "dry_fit_null_db": nulldb}

    # capture trajectory (device-rate samples)
    tc, d_cap_s, cap_slope = extract_trajectory(cap_ch, fs_sig, t0, t1)
    d_cap = d_cap_s * device_fs

    # model trajectory at phase=0 (baseline, for period fit + phase-fit correlation)
    tm, d_model0, _ = render_trajectory(spec, x, fs_sig, t0, t1, device_fs, phase=0.0, dry=dry_ref)

    n = min(len(d_cap), len(d_model0))
    d_cap, d_model0, tc = d_cap[:n], d_model0[:n], tc[:n]

    # LFO period of each, same synchronous method, from an FFT-seeded grid search
    f_guess_cap = fft_rate_guess(tc, d_cap)
    f_guess_mod = fft_rate_guess(tc, d_model0)
    rate_cap_hz, period_cap_s = fit_period(tc, d_cap, f_guess_cap)
    rate_model_hz, period_model_s = fit_period(tc, d_model0, f_guess_mod)

    # phase fit: fold each trajectory to a one-cycle template at the MODEL's
    # own fitted period (256 bins, same synchronous-average machinery as the
    # period fit above), then circularly cross-correlate the two templates --
    # this is "circular over the LFO period" directly, at negligible cost
    # (256-point FFT) instead of a shift search over the full ~1.4M-sample
    # trajectory.
    cap_template = synchronous_average(tc, d_cap, period_model_s)
    model_template = synchronous_average(tc, d_model0, period_model_s)
    shift_cycles, corr_score = circular_template_fit(cap_template, model_template)
    # engine LFO: value(t) = g(phase0 + rate*t) => phase0=d is a time-shift of
    # d/rate seconds, i.e. m_d(t) = m_0(t + d*period) = m_0 delayed by -d*period.
    # circular_template_fit's shift_cycles is how much model_template must be
    # DELAYED (in cycles) to match cap_template, so -d = shift_cycles.
    phase_a = (-shift_cycles) % 1.0
    phase_b = (1.0 - phase_a) % 1.0

    cand = {}
    for tag, ph in (("a", phase_a), ("b", phase_b)):
        _, d_model_ph, _ = render_trajectory(spec, x, fs_sig, t0, t1, device_fs, phase=ph, dry=dry_ref)
        d_model_ph = d_model_ph[:n]
        offset = float(np.mean(d_cap - d_model_ph))
        resid = d_cap - (d_model_ph + offset)
        cand[tag] = (ph, offset, rms(resid), d_model_ph)
    tag = "a" if cand["a"][2] <= cand["b"][2] else "b"
    fitted_phase, offset_samples, _, d_model = cand[tag]

    diff = d_cap - (d_model + offset_samples)
    T = n / fs_sig
    mid0 = max(0.0, (T - WIN_S) / 2.0)
    windows = {
        "start": window_slice(n, fs_sig, 0.0, WIN_S),
        "middle": window_slice(n, fs_sig, mid0, WIN_S),
        "end": window_slice(n, fs_sig, T - WIN_S, WIN_S),
    }

    def wstats(a, b):
        d = diff[a:b]
        return rms(d), float(np.max(np.abs(d))) if len(d) else float("nan")

    win_stats = {k: wstats(a, b) for k, (a, b) in windows.items()}

    # residual spectrum: periodic component at the LFO rate / harmonics vs a
    # broadband floor away from those bins (structured shape-error check)
    nfft = 1 << (int(np.ceil(np.log2(n))) + 3)
    R = np.abs(np.fft.rfft(diff * np.hanning(n), nfft))
    fr = np.fft.rfftfreq(nfft, 1.0 / fs_sig)
    floor_mask = (fr > 0.3) & (fr < 25.0)
    floor = np.median(R[floor_mask])
    harmonics = {}
    for h in (1, 2, 3):
        target = rate_model_hz * h
        idx = int(np.argmin(np.abs(fr - target)))
        win = 3
        peak = R[max(0, idx - win):idx + win + 1].max()
        harmonics[h] = db(peak / (floor + 1e-20))

    rate_ppm = (rate_cap_hz / rate_model_hz - 1.0) * 1e6

    return {
        "name": name,
        "shape": shape_note,
        "align": ainfo,
        "dry_subtraction": dry_info,
        "fitted_phase_cycles": fitted_phase,
        "phase_candidate_used": tag,
        "shift_cycles": shift_cycles,
        "template_corr": corr_score,
        "offset_samples": offset_samples,
        "rms_diff_whole": rms(diff),
        "peak_diff_whole": float(np.max(np.abs(diff))),
        "ptp_capture": float(d_cap.max() - d_cap.min()),
        "ptp_model": float(d_model.max() - d_model.min()),
        "rate_capture_hz": rate_cap_hz,
        "rate_model_hz": rate_model_hz,
        "period_capture_s": period_cap_s,
        "period_model_s": period_model_s,
        "rate_ppm_error": rate_ppm,
        "windows": win_stats,
        "harmonics_db": harmonics,
        "n_samples": n,
        "duration_s": T,
        "cap_removed_slope_s_per_s": cap_slope,
    }


def report(r):
    print(f"=== {r['name']}  shape={r['shape']} ===")
    print(f"align: {r['align'].get('method')} offset {r['align'].get('offset_s', 0):.4f} s")
    if r.get("dry_subtraction"):
        di = r["dry_subtraction"]
        print(f"dry subtracted: gain {di['gain']:.5f} ({di['gain_db']:+.2f} dB) at lag {di['lag_samples']:+.4f} samples, "
              f"fitted on the first {di['dry_window_ms']:.0f} ms after each click/burst, residual there "
              f"{di['dry_fit_null_db']:+.1f} dB")
    print(f"fitted LFO phase: {r['fitted_phase_cycles']:.4f} cycles "
          f"(candidate {r['phase_candidate_used']}, template shift {r['shift_cycles']:.4f} cycles, "
          f"corr {r['template_corr']:.4g})")
    print(f"constant offset: {r['offset_samples']:.4f} device-samples")
    print(f"whole-tone ({r['duration_s']:.2f} s, n={r['n_samples']}): "
          f"rms diff {r['rms_diff_whole']:.4f}  peak diff {r['peak_diff_whole']:.4f}  device-samples")
    for k in ("start", "middle", "end"):
        rmsv, pk = r["windows"][k]
        print(f"  window {k:6s}: rms {rmsv:.4f}  peak {pk:.4f}  device-samples")
    print(f"p-p capture {r['ptp_capture']:.4f}  p-p model {r['ptp_model']:.4f}  device-samples")
    print(f"LFO rate: capture {r['rate_capture_hz']:.6f} Hz (period {r['period_capture_s']*1000:.4f} ms)  "
          f"model {r['rate_model_hz']:.6f} Hz (period {r['period_model_s']*1000:.4f} ms)  "
          f"rate error {r['rate_ppm_error']:+.1f} ppm")
    for h, val in r["harmonics_db"].items():
        print(f"  residual peak at {h}x LFO rate vs floor: {val:+.2f} dB")
    print()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture")
    ap.add_argument("--model", default=os.path.join(ROOT, "models", "ax30g-modd.json"))
    ap.add_argument("--signal", default=os.path.join(ROOT, "capture", "signalset-lfo.wav"))
    ap.add_argument("--layout", default=os.path.join(ROOT, "capture", "layout-lfo.json"))
    ap.add_argument("--force-shape", default=None, help="override blocks.lfo.shape in memory only (e.g. sin)")
    ap.add_argument("--subtract-dry", action="store_true",
                    help="fit and remove the dry path from BOTH capture and model before tracking. Required for a "
                         "mixed dry+wet effect such as CHO: a pitch track of dry+wet follows the beating between "
                         "them, not the delay. Uses maps.wet (CHO) or maps.balance (SDLY/SMOD) zeroed for the "
                         "model's dry render; not valid for MODD below Balance 50 (see zero_wet).")
    ap.add_argument("--dry-window-ms", type=float, default=20.0,
                    help="length of the dry-only window after each click and burst onset used to fit the dry "
                         "gain and lag (default 20 ms; must be shorter than the effect's shortest delay -- the "
                         "Chorus' is 24.04 ms)")
    a = ap.parse_args()
    r = analyze(a.capture, a.model, a.signal, a.layout, force_shape=a.force_shape,
                subtract_dry=a.subtract_dry, dry_window_ms=a.dry_window_ms)
    report(r)


if __name__ == "__main__":
    main()
