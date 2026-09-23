#!/usr/bin/env python3
"""HYPR grid null test: render every captured Hyper Resonator setting through
the model and compare it with the real capture, one row per capture.

    tests/hypr_null.py
    tests/hypr_null.py --only Type-2
    tests/hypr_null.py --jobs 1
    tests/hypr_null.py --md docs/hypr-null-2026-09-18.md

A fuzz will not null.  A time-domain null needs the model's harmonics to
agree with the unit's in PHASE as well as level; a clipper's harmonics fold
back around the device's 19.5 kHz Nyquist, and a sample of timing error
there is a radian of phase.  So this test reports the per-segment null the
way the other block tests do AND four residuals that are meaningful for a
nonlinear block:

* `curve`   -- transfer-curve residual: the output's 1 kHz component over
               the 5 s ramp, model against capture, dB rms over the ramp
               after one gain is fitted.  This is the describing function
               and is the number to read for the driver.
* `H2..H5`  -- harmonic-level residuals on the steady -20 dBFS sine, dB.
* `cents`   -- the resonator's peak-frequency trajectory error, cents rms,
               tracked on the ramp and on the four burst decays.
* `sgram`   -- the DI clip's log-spectrogram distance, dB rms (1/3-octave
               bands, 20 ms frames), which is the listening-relevant number.

The ramp is excluded from the broadband gain fit, as in tests/eq3_null.py
and tests/comp_null.py: it is the segment that reaches the ceiling.
"""
import argparse
import json
import os
import re
import sys
import numpy as np
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

SEGMENTS = ("clicks", "bursts", "sine20", "sine40", "ramp", "sweep", "noise", "di")


def params_from_name(name):
    b = os.path.basename(name)
    out = {}
    for k, j in (("Type", "Type"), ("Harmonics", "Harmonics"), ("Sensitivity", "Sensitivity"),
                 ("Depth", "Depth"), ("Decay", "Decay"), ("Resonance", "Resonance"),
                 ("DirectLevel", "Direct Level"), ("EffectLevel", "Effect Level")):
        m = re.search(k + r"-(-?\d+)", b)
        if m:
            out[j] = int(m.group(1))
    m = re.search(r"Polarity-(UP|DOWN)", b)
    if m:
        out["Polarity"] = m.group(1)
    return out


def rows_from_captures(capdir, only=None):
    out = []
    for n in sorted(os.listdir(capdir)):
        if not n.startswith("AX30G_HYPR_") or not n.endswith(".wav"):
            continue
        if only and only not in n:
            continue
        # the take-1 MAX capture was made before the input knob was moved
        if "IN-MAX" in n and "_take2" not in n:
            continue
        out.append(os.path.join(capdir, n))
    return out


def _pin_threads():
    for v in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS",
              "VECLIB_MAXIMUM_THREADS", "NUMEXPR_NUM_THREADS"):
        os.environ.setdefault(v, "1")


# ---------------------------------------------------------------- measures
def tone_curve(y, fs, layout, k=1):
    """The 1 kHz component of `y` through the ramp, dB, one value per 21 ms."""
    from scipy.signal import stft
    from analysis.util import seg, cut
    rp = seg(layout, "ramp")
    c = cut(y, fs, rp["start"], rp["end"])
    f, t, Z = stft(c, fs, nperseg=4096, noverlap=4096 - 1024)
    i = int(round(1000 * k * 4096 / fs))
    a = np.max(np.abs(Z[i - 2:i + 3, :]), axis=0)
    return 20 * np.log10(np.maximum(a, 1e-12))


def harmonics(y, fs, t0=23.0, t1=25.0, nh=5):
    from analysis.util import cut
    c = cut(y, fs, t0, t1)
    n = len(c)
    w = np.hanning(n)
    Y = np.abs(np.fft.rfft(c * w)) / (np.sum(w) / 2)
    out = []
    for k in range(1, nh + 1):
        i = int(round(1000 * k * n / fs))
        out.append(20 * np.log10(max(float(np.max(Y[i - 3:i + 4])), 1e-14)))
    return np.array(out)


def peak_track(y, fs, t0, t1, fmin=150.0, fmax=12000.0, nper=1024, hop=256):
    from scipy.signal import stft
    from analysis.util import cut
    c = cut(y, fs, t0, t1)
    f, t, Z = stft(c, fs, nperseg=nper, noverlap=nper - hop)
    A = np.abs(Z)
    m = (f >= fmin) & (f <= fmax)
    fr = f[m]
    A = A[m]
    ks = np.argmax(A, axis=0)
    return t + t0, fr[ks], 20 * np.log10(np.maximum(A[ks, np.arange(A.shape[1])], 1e-12))


def sgram_bands(y, fs, t0, t1, nb=24, frame=0.020):
    from analysis.util import cut
    c = cut(y, fs, t0, t1)
    n = int(frame * fs)
    edges = np.geomspace(60.0, 16000.0, nb + 1)
    rows = []
    for i in range(0, len(c) - n, n):
        X = np.abs(np.fft.rfft(c[i:i + n] * np.hanning(n)))
        f = np.fft.rfftfreq(n, 1.0 / fs)
        r = [np.sqrt(np.mean(X[(f >= edges[j]) & (f < edges[j + 1])] ** 2)) if ((f >= edges[j]) & (f < edges[j + 1])).any() else 0.0
             for j in range(nb)]
        rows.append(r)
    return 20 * np.log10(np.maximum(np.array(rows), 1e-9))


def row_for(job):
    p, spec_path, signal, layout_path, overrides = job
    overrides = dict(overrides or {})
    _pin_threads()
    import json as _json
    import numpy as _np
    import soundfile as _sf
    from analysis.util import load_layout as _load_layout, seg as _seg, cut as _cut
    from analysis.run import load_aligned as _load_aligned
    from engine.render import load_spec as _load_spec, render_spec as _render_spec
    import engine.hypr                                              # noqa: F401
    from tests.null_test import best_shift as _best, refine_lag as _refine, \
        frac_shift as _shift, db as _db

    x, fs = _sf.read(signal, dtype="float64", always_2d=True)
    x = x[:, 0]
    layout = _load_layout(layout_path)
    segs = [s for s in layout["segments"] if s["name"] != "silence"]

    name = os.path.splitext(os.path.basename(p))[0]
    spec = _json.loads(_json.dumps(_load_spec(spec_path)))
    spec["params"].update(params_from_name(name))
    if "IN-MAX" in name:
        st = dict(spec.get("input_stage") or {})
        st["input_level_db"] = 14.0509
        spec["input_stage"] = st
    for k, v in (overrides or {}).items():
        cur = spec
        parts = k.split(".")
        for q in parts[:-1]:
            cur = cur.setdefault(q, {})
        cur[parts[-1]] = v

    y, truth = _render_spec(spec, x, fs, spec_path=spec_path)
    cap, _ai = _load_aligned(p, x, fs, layout)
    c = cap[: len(y), 0]
    m = y[: len(c), 0]

    mask = _np.ones(len(c), bool)
    rp = _seg(layout, "ramp")
    mask[int(rp["start"] * fs):int(rp["end"] * fs)] = False

    # The block's latency is fixed, so the lag is NOT fitted per row: on an
    # effect-only row the model and the capture are barely correlated and a
    # free cross-correlation locks onto noise (it ran to the +-96-sample
    # search bound on 9 of the 21 rows). It is measured once, on the
    # Direct-50 / Effect-0 capture -- the one row that is linear -- and
    # reused. `--lag` overrides it.
    lag = float(overrides.pop("_lag", 14.87)) if overrides else 14.87
    m2 = _shift(m, lag)
    g = float(_np.dot(c[mask], m2[mask]) / max(_np.dot(m2[mask], m2[mask]), 1e-20))
    r = c - g * m2

    row = {"capture": name, "params": spec["params"], "lag_samples": lag,
           "input": "MAX" if "IN-MAX" in name else "LIN",
           "gain_db": _db(abs(g)), "segments": {},
           "drive_gain_db": truth.get("drive_gain_db"), "Q": truth.get("Q"),
           "f_max_track": truth.get("f_max_track")}
    for s in segs:
        cs = _cut(c, fs, s["start"], s["end"])
        rs = _cut(r, fs, s["start"], s["end"])
        row["segments"][s["name"]] = _db(_np.sqrt(_np.mean(rs ** 2))) - _db(_np.sqrt(_np.mean(cs ** 2)))
    row["total"] = _db(_np.sqrt(_np.mean(r[mask] ** 2))) - _db(_np.sqrt(_np.mean(c[mask] ** 2)))

    # --- transfer curve (describing function) on the ramp -----------------
    cc = tone_curve(c, fs, layout)
    mc = tone_curve(g * m2, fs, layout)
    good = (cc > cc.max() - 45) & (mc > -200)
    row["curve_rms_db"] = float(_np.sqrt(_np.mean((mc[good] - cc[good] - _np.mean(mc[good] - cc[good])) ** 2))) if good.sum() > 8 else None

    # --- harmonic levels on the steady sine -------------------------------
    hc = harmonics(c, fs)
    hm = np.maximum(harmonics(g * m2, fs), -140.0)
    row["harm_capture"] = [float(v) for v in hc]
    row["harm_model"] = [float(v) for v in hm]
    live = hc > -108.0
    row["harm_rms_db"] = float(_np.sqrt(_np.mean((hm[live] - hc[live]) ** 2))) if live.any() else None

    # --- resonator trajectory, cents --------------------------------------
    cents = []
    for t0, t1 in [(rp["start"], rp["end"])] + [(b, b + 0.5) for b in _seg(layout, "bursts")["burst_times"]]:
        tc, fc_, ac = peak_track(c, fs, t0, t1)
        tm, fm_, am = peak_track(g * m2, fs, t0, t1)
        n = min(len(fc_), len(fm_))
        sel = (ac[:n] > ac[:n].max() - 30) & (am[:n] > am[:n].max() - 30)
        if sel.sum() > 8:
            cents.append(float(_np.sqrt(_np.mean((1200 * _np.log2(fm_[:n][sel] / fc_[:n][sel])) ** 2))))
    row["cents_rms"] = float(_np.median(cents)) if cents else None

    # --- DI spectrogram distance ------------------------------------------
    di = _seg(layout, "di")
    Sc = sgram_bands(c, fs, di["start"], di["end"])
    Sm = sgram_bands(g * m2, fs, di["start"], di["end"])
    n = min(len(Sc), len(Sm))
    live2 = Sc[:n] > (Sc[:n].max() - 60)
    row["sgram_rms_db"] = float(_np.sqrt(_np.mean((Sm[:n][live2] - Sc[:n][live2]) ** 2))) if live2.any() else None
    return row


def short(p):
    return ("T{Type} H{Harmonics:<2} S{Sensitivity:<2} {Polarity:<4} D{Depth:<2} "
            "Dec{Decay:<2} R{Resonance:<2} Dir{Direct Level:<2} Eff{Effect Level:<2}").format(**p)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", default=os.path.join(ROOT, "models", "ax30g-hypr.json"))
    ap.add_argument("--signal", default=os.path.join(ROOT, "capture", "signalset-normal.wav"))
    ap.add_argument("--layout", default=os.path.join(ROOT, "capture", "layout-normal.json"))
    ap.add_argument("--captures", default=os.path.join(ROOT, "captures"))
    ap.add_argument("--only")
    ap.add_argument("--lag", type=float, default=14.87,
                    help="fixed alignment lag in samples at 48 kHz (measured on the Direct-only row)")
    ap.add_argument("--set", action="append", default=[],
                    help="override a spec key, e.g. --set blocks.sweep.octaves_per_db=0.6")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    ap.add_argument("--json")
    ap.add_argument("--md")
    a = ap.parse_args()

    paths = rows_from_captures(a.captures, a.only)
    if not paths:
        raise SystemExit("no HYPR captures found")
    ov = {"_lag": a.lag}
    for s in a.set:
        k, _, v = s.partition("=")
        try:
            v = json.loads(v)
        except Exception:
            pass
        ov[k] = v

    jobs = [(p, os.path.abspath(a.model), a.signal, a.layout, ov) for p in paths]
    if a.jobs > 1 and len(jobs) > 1:
        with ProcessPoolExecutor(max_workers=min(a.jobs, len(jobs))) as ex:
            rows = list(ex.map(row_for, jobs))
    else:
        rows = [row_for(j) for j in jobs]

    for row in rows:
        s = row["segments"]
        print("%s %s | gain %+6.2f lag %+6.2f | ramp %+6.1f sine20 %+6.1f sweep %+6.1f di %+6.1f all %+6.1f | "
              "curve %5s harm %5s cents %6s sgram %5s"
              % (short(row["params"]), row["input"], row["gain_db"], row["lag_samples"], s["ramp"], s["sine20"],
                 s["sweep"], s["di"], row["total"],
                 "%.1f" % row["curve_rms_db"] if row["curve_rms_db"] is not None else "-",
                 "%.1f" % row["harm_rms_db"] if row["harm_rms_db"] is not None else "-",
                 "%.0f" % row["cents_rms"] if row["cents_rms"] is not None else "-",
                 "%.1f" % row["sgram_rms_db"] if row["sgram_rms_db"] is not None else "-"))

    def med(f):
        v = [f(r) for r in rows if f(r) is not None]
        return float(np.median(v)) if v else float("nan")
    print("\n%d rows. median: overall %+.1f dB, sine20 %+.1f, sweep %+.1f, di %+.1f | "
          "curve %.1f dB, harm %.1f dB, cents %.0f, sgram %.1f dB"
          % (len(rows), med(lambda r: r["total"]), med(lambda r: r["segments"]["sine20"]),
             med(lambda r: r["segments"]["sweep"]), med(lambda r: r["segments"]["di"]),
             med(lambda r: r["curve_rms_db"]), med(lambda r: r["harm_rms_db"]),
             med(lambda r: r["cents_rms"]), med(lambda r: r["sgram_rms_db"])))

    if a.json:
        with open(a.json, "w") as f:
            json.dump(rows, f, indent=1, default=float)
    if a.md:
        import datetime
        L = [f"# HYPR null test against the real captures — {datetime.date.today().isoformat()}", "",
             "`tests/hypr_null.py --md <this file>`. Each row renders its own captured setting through "
             "`models/ax30g-hypr.json` (converter chain, input stage, pre-emphasised domain, no output "
             "de-emphasis), aligns at a FIXED lag of 14.87 samples at 48 kHz — measured once on the "
             "Direct-50 / Effect-0 capture, the only linear row in the grid, because a free "
             "cross-correlation runs to its ±96-sample search bound on 9 of the 21 rows — fits one "
             "broadband gain, and reports residual rms minus capture rms per segment. More negative is "
             "better; the ramp is excluded from the gain fit. The take-1 IN-MAX capture is excluded by "
             "name (it was recorded before the input knob was moved; `_take2` is the valid one).", "",
             "**A fuzz does not null.** The four columns after the null are the ones that mean something "
             "for a nonlinear, time-varying block: `curve` is the 1 kHz describing function over the ramp "
             "(dB rms, mean removed), `harm` the H1..H5 level residual on the steady −20 dBFS sine (dB "
             "rms), `cents` the resonator peak-frequency trajectory error (cents rms, median of the ramp "
             "and the four burst decays), `sgram` the DI clip's 1/3-octave log-spectrogram distance "
             "(dB rms).", "",
             "| Type | Harm | Sens | Pol | Depth | Decay | Res | Dir | Eff | in | gain dB | ramp | sine20 | sweep | noise | di | all | curve | harm | cents | sgram |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
        for r in rows:
            p = r["params"]
            s = r["segments"]
            def f(v, fmt="%.1f"):
                return fmt % v if v is not None else "—"
            L.append("| {} | {} | {} | {} | {} | {} | {} | {} | {} | {} | {:+.2f} | {:+.1f} | {:+.1f} | {:+.1f} | {:+.1f} | {:+.1f} | {:+.1f} | {} | {} | {} | {} |".format(
                p["Type"], p["Harmonics"], p["Sensitivity"], p["Polarity"], p["Depth"], p["Decay"],
                p["Resonance"], p["Direct Level"], p["Effect Level"], r["input"], r["gain_db"],
                s["ramp"], s["sine20"], s["sweep"], s["noise"], s["di"], r["total"],
                f(r["curve_rms_db"]), f(r["harm_rms_db"]), f(r["cents_rms"], "%.0f"), f(r["sgram_rms_db"])))
        L += ["", "**Median: overall %+.1f dB, sine20 %+.1f, sweep %+.1f, di %+.1f; curve %.1f dB, harm %.1f dB, cents %.0f, sgram %.1f dB.**"
              % (med(lambda r: r["total"]), med(lambda r: r["segments"]["sine20"]),
                 med(lambda r: r["segments"]["sweep"]), med(lambda r: r["segments"]["di"]),
                 med(lambda r: r["curve_rms_db"]), med(lambda r: r["harm_rms_db"]),
                 med(lambda r: r["cents_rms"]), med(lambda r: r["sgram_rms_db"]))]
        with open(a.md, "w") as f:
            f.write("\n".join(L) + "\n")
        print("wrote", a.md)


if __name__ == "__main__":
    main()
