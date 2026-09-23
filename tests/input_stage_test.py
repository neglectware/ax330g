#!/usr/bin/env python3
"""Measure the analog input stage from the Bypass LIN/MAX pair and check the
model of it. Every number in docs/input-stage-2026-09-16.md comes from here.

    input_stage_test.py measure          # the measurement, from the captures only
    input_stage_test.py fit              # scan the model's parameters on the ramp
    input_stage_test.py check            # model vs capture: ramp, THD, clicks, sweep

All three default to captures/AX30G_BYPASS_IN-{LIN,MAX}.wav, the normal signal
set and models/ax30g-bypass.json + models/ax30g-input-stage.json.
"""
import argparse
import json
import os
import sys

import numpy as np
import soundfile as sf
from scipy.signal import fftconvolve

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
from analysis.util import load_layout, seg, cut, db          # noqa: E402
from analysis.run import load_aligned                        # noqa: E402
from analysis.input_stage import (amp_at, gain_vs_level, fit_threshold, fit_shelf,   # noqa: E402
                                  shelf_from_sweep, shelf_from_harmonics, harmonics,
                                  hardclip_fund_db, shelf_model_db, DEVICE_RATE)
from engine.render import load_spec, render_spec             # noqa: E402
from tests.null_test import best_shift, refine_lag, frac_shift  # noqa: E402

MAX_DB = 14.0509     # Input Level fully clockwise, relative to LIN


def load(args):
    x, fs = sf.read(args.signal, dtype="float64", always_2d=True)
    x = x[:, 0]
    layout = load_layout(args.layout)
    lin, _ = load_aligned(args.lin, x, fs, layout)
    mx, _ = load_aligned(args.max, x, fs, layout)
    return x, fs, layout, lin[:, 0], mx[:, 0]


def cmd_measure(args):
    x, fs, layout, L, M = load(args)
    print("== small-signal gain, MAX over LIN (sweep, 30 ms windows)")
    s = seg(layout, "sweep")
    f0, f1, sec = 20.0, 19000.0, s["end"] - s["start"]
    k = np.log(f1 / f0)
    lo = []
    for t in np.arange(0.02, sec - 0.04, 0.05):
        f = f0 * np.exp(t * k / sec)
        a = cut(M, fs, s["start"] + t, s["start"] + t + 0.03)
        b = cut(L, fs, s["start"] + t, s["start"] + t + 0.03)
        d = db(np.sqrt(np.mean(a ** 2))) - db(np.sqrt(np.mean(b ** 2)))
        if f < 7000:
            lo.append(d)
    print(f"   MAX - LIN below 7 kHz: {np.mean(lo):.4f} dB, spread {np.std(lo):.4f} dB, n={len(lo)}")

    print("== ramp: fundamental compression per 0.1 s (MAX vs LIN + %.4f dB)" % MAX_DB)
    rows = gain_vs_level(M, L, fs, layout, MAX_DB)
    thr, err = fit_threshold(rows)
    print(f"   hard-clip threshold {thr:.4f} dBFS in the capture's scale, rms error {err:.4f} dB")
    print("     t   unclipped  measured  compression   hard-clip   err")
    for t, U, meas, c in rows:
        if U > -10:
            p = float(hardclip_fund_db(U - thr))
            print(f"   {t:5.2f} {U:9.2f} {meas:9.2f} {c:12.3f} {p:11.3f} {c - p:7.3f}")

    print("== sweep: per-frequency onset -> pre-emphasis relative to 1 kHz")
    sw = shelf_from_sweep(M, L, fs, layout, thr, MAX_DB)
    for f, U, c, o, S in sw:
        print(f"   {f:8.1f} Hz  comp {c:7.3f} dB  over {o:6.3f} dB  shelf {S:6.2f} dB")

    print("== ramp harmonics vs the analytic hard clip -> pre-emphasis at 3f, 5f ...")
    hm = shelf_from_harmonics(M, L, fs, layout, thr, MAX_DB)
    for n, (v, sd, cnt) in hm.items():
        print(f"   {n} kHz: {v:+6.2f} dB (spread {sd:.2f}, n={cnt})")

    # weights: a harmonic point is worth 1/its own spread across the ramp
    # windows (H3 is tight, H11 upwards is not); a sweep point is worth 1
    # where the compression is above 0.15 dB and 0.4 where it is smaller than
    # that and the inversion is sensitive to noise.
    pts = [(1000.0 * n, v, 1.0 / max(sd, 0.25)) for n, (v, sd, cnt) in hm.items()]
    pts += [(f, S, 1.0 if abs(c) > 0.15 else 0.4) for f, U, c, o, S in sw]
    t1, t2, rms = fit_shelf(pts)
    print(f"== shelf fit: tau_zero {t1:.2f} us ({1e6 / (2 * np.pi * t1):.0f} Hz), "
          f"tau_pole {t2:.2f} us ({1e6 / (2 * np.pi * t2):.0f} Hz), "
          f"shelf {20 * np.log10(t1 / t2):.2f} dB, rms {rms:.3f} dB")
    for f in [100, 500, 1000, 2000, 3000, 5000, 8000, 10000, 12000, 15000, 17000, 19000]:
        print(f"   {f:6d} Hz  {shelf_model_db(f, t1, t2):6.2f} dB re DC   "
              f"{shelf_model_db(f, t1, t2) - shelf_model_db(1000, t1, t2):6.2f} dB re 1 kHz")

    print("== Peak LED check (manual: OVLD at full scale -1 dB)")
    clip_in = thr - MAX_DB - 7.0510 + 7.0510   # thr is already in output scale
    print("   input dBFS at which a sine clips / lights Peak, by frequency and setting")
    g1k = 7.0510
    for f in [100, 1000, 4000, 10000, 16000]:
        rel = shelf_model_db(f, t1, t2) - shelf_model_db(1000, t1, t2)
        for name, lvl in (("MAX", MAX_DB), ("LIN", 0.0)):
            clip_dbfs = thr - g1k + (MAX_DB - lvl) - rel
            print(f"     {f:6d} Hz {name}: clip {clip_dbfs:+7.2f} dBFS, Peak {clip_dbfs - 1.0:+7.2f} dBFS")


def render_model(spec_path, stage, x, fs, eq=None, level_db=MAX_DB):
    spec = load_spec(spec_path)
    st = json.loads(json.dumps(stage))
    st["input_level_db"] = level_db
    spec["input_stage"] = st
    y, truth = render_spec(spec, x, fs, spec_path=spec_path)
    if eq is not None:
        y = np.stack([fftconvolve(y[:, c], eq, mode="full")[: y.shape[0]] for c in range(2)], axis=1)
    return y[:, 0], truth


def fit_align(cap, model, fs, fit_lo, fit_hi, lag_lo=None, lag_hi=None):
    """Align model to cap and fit one gain. The lag is fitted on `lag_*` (a
    broadband window: a 1 kHz ramp alone only determines the lag modulo one
    period, 48 samples at 48 kHz, and a 3-period error nulls the ramp just as
    well while wrecking every other segment). The gain is fitted on
    `fit_*`, which must be UNCLIPPED signal: a gain fitted over clipped
    samples trades directly against the ceiling and hides it."""
    n = min(len(cap), len(model))
    cap, model = cap[:n], model[:n]
    mask = np.zeros(n, bool)
    mask[int(fit_lo * fs):int(fit_hi * fs)] = True
    lmask = mask if lag_lo is None else np.zeros(n, bool)
    if lag_lo is not None:
        lmask[int(lag_lo * fs):int(lag_hi * fs)] = True
    lag0 = best_shift(cap[lmask], model[lmask], fs)
    lag = refine_lag(cap, model, lag0, lmask)
    m2 = frac_shift(model, lag)
    g = float(np.dot(cap[mask], m2[mask]) / np.dot(m2[mask], m2[mask]))
    return g * m2, lag, g


def cmd_fit(args):
    """Scan the model's parameters against the clipped part of the MAX ramp.
    The gain is fitted on the UNCLIPPED part of the same ramp, not on the
    clipped part: fitting one gain over clipped samples trades directly
    against the ceiling and hides it (a 0.35 dB ceiling change moved the
    score by 0.04 dB when the gain was free)."""
    x, fs, layout, L, M = load(args)
    eq = np.load(args.eq) if args.eq else None
    stage = json.load(open(args.stage))
    s = seg(layout, "ramp")
    lead = 0.6
    i0, i1 = int((s["start"] - lead) * fs), int((s["end"] + 0.3) * fs)
    xs, capL = x[i0:i1], M[i0:i1]

    def score(st):
        m, _ = render_model(args.model, st, xs, fs, eq)
        m2, lag, g = fit_align(capL, m, fs, lead + 0.5, lead + 3.0, 0.0, lead + 0.4)
        ev = np.zeros(len(capL), bool)
        ev[int((lead + 3.2) * fs):int((lead + 4.95) * fs)] = True
        r = capL - m2
        return (db(np.sqrt(np.mean(r[ev] ** 2))) - db(np.sqrt(np.mean(capL[ev] ** 2))),
                db(abs(g)))

    def variant(**kw):
        st = json.loads(json.dumps(stage))
        for k, v in kw.items():
            if k in ("tau_zero_us", "tau_pole_us"):
                st["pre_emphasis"][k] = v
            else:
                st["ceiling"][k] = v
        return st

    print("== headroom_dbfs")
    for h in args.headroom:
        v, g = score(variant(headroom_dbfs=h))
        print(f"   {h:6.3f} -> {v:7.2f} dB   (gain {g:+.3f})")
    print("== offset_frac")
    best = None
    for o in args.offset:
        v, _ = score(variant(offset_frac=o))
        print(f"   {o:+.4f} -> {v:7.2f} dB")
        if best is None or v < best[0]:
            best = (v, o)
    print(f"   best {best[1]:+.4f} at {best[0]:.2f} dB")
    print("== pre-emphasis time constants (us), at that offset")
    rows = []
    for t1 in args.tau_zero:
        for t2 in args.tau_pole:
            v, _ = score(variant(offset_frac=best[1], tau_zero_us=t1, tau_pole_us=t2))
            rows.append((v, t1, t2))
            print(f"   {t1:6.2f} {t2:6.2f} -> {v:7.2f} dB")
    rows.sort()
    print(f"   best {rows[0][1]:.2f} / {rows[0][2]:.2f} us at {rows[0][0]:.2f} dB")


def cmd_check(args):
    x, fs, layout, L, M = load(args)
    eq = np.load(args.eq) if args.eq else None
    stage = json.load(open(args.stage))
    modes = {
        "full": stage,
        "no_shelf": {**stage, "pre_emphasis": {},
                     "ceiling": {**stage["ceiling"], "headroom_dbfs": stage["ceiling"]["headroom_dbfs"] - 0.593}},
        # "no_clip" must disable the whole stage: leaving the headroom gain in
        # and only turning the ceiling off does not give a linear model,
        # because _converter's own +-1 saturation (the same 18-bit full scale)
        # then does the clipping instead.
        "no_clip": {**stage, "enabled": False},
        "input_rate": {**stage, "rate": "input"},
    }
    s = seg(layout, "ramp")
    for name in args.modes:
        st = modes[name]
        m, truth = render_model(args.model, st, x, fs, eq)
        sw0 = seg(layout, "sweep")
        m2, lag, g = fit_align(M, m, fs, s["start"] + 0.5, s["start"] + 3.0,
                               sw0["start"], sw0["end"])
        n = len(m2)
        Mc, Lc = M[:n], L[:n]
        r = Mc - m2
        print(f"== {name}: lag {lag:+.3f}, gain {db(abs(g)):+.3f} dB, "
              f"Peak LED {truth.get('input_stage', {}).get('peak_led')}, "
              f"clipped samples {truth.get('input_stage', {}).get('clipped_samples')}")
        for sg in layout["segments"]:
            if sg["name"] == "silence":
                continue
            c, rr = cut(Mc, fs, sg["start"], sg["end"]), cut(r, fs, sg["start"], sg["end"])
            print(f"   {sg['name']:8s} capture {db(np.sqrt(np.mean(c ** 2))):6.1f}  "
                  f"residual {db(np.sqrt(np.mean(rr ** 2))):6.1f}  "
                  f"null {db(np.sqrt(np.mean(rr ** 2))) - db(np.sqrt(np.mean(c ** 2))):+6.1f} dB")
        if name != args.modes[0]:
            continue
        print("   -- ramp, per 0.1 s: capture rms, residual, null, fundamentals")
        nn = int(0.1 * fs)
        for k in range(0, int((s["end"] - s["start"]) * fs) - nn, nn):
            i = int(s["start"] * fs) + k
            a, b, rr = Mc[i:i + nn], m2[i:i + nn], r[i:i + nn]
            print(f"     t {k / fs:5.2f}  cap {db(np.sqrt(np.mean(a ** 2))):7.2f}  "
                  f"res {db(np.sqrt(np.mean(rr ** 2))):7.2f}  null {db(np.sqrt(np.mean(rr ** 2))) - db(np.sqrt(np.mean(a ** 2))):+6.2f}  "
                  f"capF {db(amp_at(a, fs, 1000, 40)):7.2f}  modF {db(amp_at(b, fs, 1000, 40)):7.2f}  "
                  f"d {db(amp_at(b, fs, 1000, 40)) - db(amp_at(a, fs, 1000, 40)):+6.3f}")
        print("   -- THD at the ramp's top second (0.25 s window at t+4.5), dBFS")
        i = int((s["start"] + 4.375) * fs)
        j = i + int(0.25 * fs)
        hc, hm = harmonics(Mc[i:j], fs), harmonics(m2[i:j], fs)
        for key in hc:
            print(f"     {key[0]}{key[1]:<3d} capture {hc[key]:7.2f}  model {hm[key]:7.2f}  d {hm[key] - hc[key]:+6.2f}")
        print("   -- clicks: peak compression and band loss against the LIN capture / a linear model")
        cl = seg(layout, "clicks")
        lin_model, _ = render_model(args.model, {**st, "ceiling": {**st["ceiling"], "type": "none"}},
                                    x, fs, eq)
        lm, _, lg = fit_align(M, lin_model, fs, s["start"] + 0.5, s["start"] + 3.0,
                              sw0["start"], sw0["end"])
        for tc in cl["click_times"][:1]:
            a = cut(Mc, fs, tc - 0.002, tc + 0.010)
            b = cut(Lc, fs, tc - 0.002, tc + 0.010)
            am = cut(m2, fs, tc - 0.002, tc + 0.010)
            bm = cut(lm, fs, tc - 0.002, tc + 0.010)
            print(f"     peak: capture {db(np.abs(a).max()) - db(np.abs(b).max()) - MAX_DB:+.3f} dB, "
                  f"model {db(np.abs(am).max()) - db(np.abs(bm).max()):+.3f} dB")
            w = np.hanning(len(a))
            F = np.fft.rfftfreq(len(a), 1 / fs)
            A, B = np.abs(np.fft.rfft(a * w)), np.abs(np.fft.rfft(b * w))
            AM, BM = np.abs(np.fft.rfft(am * w)), np.abs(np.fft.rfft(bm * w))
            for lo, hi in [(50, 200), (200, 500), (500, 1000), (1000, 2000), (2000, 4000),
                           (4000, 8000), (8000, 12000), (12000, 16000), (16000, 19000)]:
                mk = (F >= lo) & (F < hi)
                ca = 20 * np.log10(np.sqrt(np.mean(A[mk] ** 2)) / np.sqrt(np.mean(B[mk] ** 2))) - MAX_DB
                cm = 20 * np.log10(np.sqrt(np.mean(AM[mk] ** 2)) / np.sqrt(np.mean(BM[mk] ** 2)))
                print(f"     {lo:6d}-{hi:<6d} capture {ca:+7.3f}  model {cm:+7.3f}  d {cm - ca:+6.3f} dB")
        print("   -- sweep: compression vs frequency, capture and model")
        sw = seg(layout, "sweep")
        f0, f1, sec = 20.0, 19000.0, sw["end"] - sw["start"]
        kk = np.log(f1 / f0)
        for t in list(np.arange(0.02, 3.5, 0.5)) + list(np.arange(3.5, sec - 0.04, 0.12)):
            f = f0 * np.exp(t * kk / sec)
            a = cut(Mc, fs, sw["start"] + t, sw["start"] + t + 0.03)
            b = cut(m2, fs, sw["start"] + t, sw["start"] + t + 0.03)
            c0 = cut(Lc, fs, sw["start"] + t, sw["start"] + t + 0.03)
            ca = db(np.sqrt(np.mean(a ** 2))) - db(np.sqrt(np.mean(c0 ** 2))) - MAX_DB
            cb = db(np.sqrt(np.mean(b ** 2))) - db(np.sqrt(np.mean(c0 ** 2))) - MAX_DB
            print(f"     {f:8.1f} Hz  capture {ca:7.3f}  model {cb:7.3f}  d {cb - ca:+6.3f} dB")


def cmd_tail(args):
    """The 20 ms tail on a hot MAX click (findings.md 2026-09-13). Measured
    from the 2026-09-12 SDLY pair, which was captured with the old -6 dBFS
    clicks -- the current signal set's -16 dBFS click barely reaches the
    ceiling -- and compared with the model's own residual for the same click.
    Both are 1 ms means of (MAX - LIN x the level difference); what is being
    compared is the SHAPE (decay and zero crossings), not the amplitude: the
    two takes are separate recordings of a single-sample click."""
    from analysis.util import load_wav
    from fractions import Fraction
    from scipy.signal import resample_poly
    base = os.path.join(ROOT, "captures",
                        "AX30G_SDLY_LDly-300_RDly-300_LFb-0_RFb-0_HighDamp-0_LBal-25_RBal-25_IN-")
    got = {}
    for tag in ("LIN", "MAX"):
        cap, fsc = load_wav(base + tag + ".wav")
        fr = Fraction(48000, int(fsc))
        cap = resample_poly(cap, fr.numerator, fr.denominator, axis=0)
        fs = 48000.0
        ref = cap[:, 2]
        i0, i1 = int(2.60 * fs), int(2.75 * fs)
        i = i0 + int(np.argmax(np.abs(ref[i0:i1])))
        got[tag] = (i, cap[:, 0], float(ref[i]))
    fs = 48000.0
    iL, L, rL = got["LIN"]
    iM, M, rM = got["MAX"]
    print(f"   reference click amplitude LIN {rL:.4f}  MAX {rM:.4f} (ratio {rM / rL:.4f})")
    n0, n1 = int(0.005 * fs), int(0.30 * fs)
    g = 10.0 ** (MAX_DB / 20.0) * (rM / rL)
    d = M[iM - n0:iM + n1] - L[iL - n0:iL + n1] * g
    w = int(0.001 * fs)
    v = np.array([np.mean(d[n0 + k * w:n0 + (k + 1) * w]) for k in range(int(n1 / w) - 1)])
    print("   measured residual, 1 ms means x1e4, t=0 at the click:")
    print("     " + " ".join(f"{q * 1e4:+.1f}" for q in v[:60]))
    zc = [k for k in range(2, len(v) - 1) if v[k] * v[k + 1] < 0]
    print(f"   measured zero crossings at {zc[:3]} ms; peak at {int(np.argmax(np.abs(v[1:]))) + 1} ms")

    stage = json.load(open(args.stage))
    x = np.zeros(int(0.5 * fs))
    x[int(0.05 * fs)] = 0.5
    ys = {}
    for lvl in (MAX_DB, 0.0):
        spec = load_spec(args.model)
        st = json.loads(json.dumps(stage))
        st["input_level_db"] = lvl
        spec["input_stage"] = st
        y, tr = render_spec(spec, x, fs, spec_path=args.model)
        ys[lvl] = (y[:, 0], tr["input_stage"])
    i = int(0.05 * fs)
    dm = ys[MAX_DB][0] - ys[0.0][0] * 10.0 ** (MAX_DB / 20.0)
    vm = np.array([np.mean(dm[i + k * w:i + (k + 1) * w]) for k in range(int(0.30 * fs / w))])
    print(f"   model: clipper peak {ys[MAX_DB][1]['clipper_peak_dbfs']:.2f} dBFS, "
          f"{ys[MAX_DB][1]['clipped_samples']} samples clipped")
    print("   model residual, same units, scaled to the measured 1-2 ms value:")
    print("     " + " ".join(f"{q / vm[1] * v[1] * 1e4:+.1f}" for q in vm[:60]))
    zm = [k for k in range(2, len(vm) - 1) if vm[k] * vm[k + 1] < 0]
    print(f"   model zero crossings at {zm[:3]} ms")
    a = 2 * np.pi * 5.491
    print(f"   analytic, a negative impulse through the chain's three {5.491} Hz high-passes: "
          f"zeros at {(3 - np.sqrt(3)) / a * 1000:.1f} and {(3 + np.sqrt(3)) / a * 1000:.1f} ms")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["measure", "fit", "check", "tail"])
    ap.add_argument("--lin", default=os.path.join(ROOT, "captures", "AX30G_BYPASS_IN-LIN.wav"))
    ap.add_argument("--max", default=os.path.join(ROOT, "captures", "AX30G_BYPASS_IN-MAX.wav"))
    ap.add_argument("--signal", default=os.path.join(ROOT, "capture", "signalset-normal.wav"))
    ap.add_argument("--layout", default=os.path.join(ROOT, "capture", "layout-normal.json"))
    ap.add_argument("--model", default=os.path.join(ROOT, "models", "ax30g-bypass.json"))
    ap.add_argument("--stage", default=os.path.join(ROOT, "models", "ax30g-input-stage.json"))
    ap.add_argument("--eq", default=os.path.join(ROOT, "out", "null", "chain-loop.npy"))
    ap.add_argument("--modes", nargs="+", default=["full", "no_shelf", "no_clip"])
    ap.add_argument("--headroom", nargs="+", type=float, default=[1.95, 2.00, 2.05, 2.07, 2.10, 2.15, 2.20])
    ap.add_argument("--offset", nargs="+", type=float, default=[-0.01, -0.005, -0.002, 0.0, 0.002, 0.005, 0.01, 0.02])
    ap.add_argument("--tau-zero", nargs="+", type=float, default=[55, 60, 64.51, 70, 75])
    ap.add_argument("--tau-pole", nargs="+", type=float, default=[15, 17, 18.39, 20, 22])
    a = ap.parse_args()
    {"measure": cmd_measure, "fit": cmd_fit, "check": cmd_check, "tail": cmd_tail}[a.cmd](a)


if __name__ == "__main__":
    main()
