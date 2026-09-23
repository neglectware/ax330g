#!/usr/bin/env python3
"""3BEQ grid null test: render every captured 3BEQ setting through the model
and null it against the real capture, one row per capture.

    tests/eq3_null.py                       # every 3BEQ capture found
    tests/eq3_null.py --grid capture/grids/3beq.json
    tests/eq3_null.py --clip-level 0.95     # scan the ceiling
    tests/eq3_null.py --only Treble-16      # substring filter on the file name
    tests/eq3_null.py --jobs 1              # serial path (see below)

Method.  The block is static, so the model-to-capture lag is a property of
the unit, not of the setting: it is measured once on the FLAT row (all bands
0, Trim 0) and then refined per row over a +-1 sample window on the residual
itself.  A single broadband gain is fitted per row (the unit's own output
gain, about -5.9 dB, is not part of the EQ model).  Everything is reported as
a null depth in dB -- residual rms minus capture rms -- per segment.

The ramp segment is excluded from the gain fit (it is the one segment that
drives the block into its clip on many rows, and a gain fitted through a
clipped segment would flatter the rest); its null is still reported.

Captures are independent (same shape as tests/rev_null.py), so each is a
`ProcessPoolExecutor` task (`--jobs`, default `cpu_count() - 2`), with
BLAS/FFT pinned to one thread per worker so the pool is not oversubscribed.
`--jobs 1` runs the old serial path and produces identical output --
verified 2026-09-18 on the full 47-row grid: `--jobs 1` (2m51s) vs `--jobs 8`
(44s) give byte-identical stdout (`diff` empty).
"""
import argparse
import json
import os
import re
import sys
import numpy as np
import soundfile as sf
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
from analysis.util import load_layout, seg, cut                 # noqa: E402
from analysis.run import load_aligned                           # noqa: E402
from engine.render import load_spec, render_spec                # noqa: E402
from tests.null_test import best_shift, refine_lag, frac_shift, params_from_name, db   # noqa: E402

FLAT = "AX30G_3BEQ_Bass-0_MidFreq-1000_MidGain-0_Treble-0_Trim-0_IN-LIN.wav"


def rows_from_captures(capdir, only=None):
    out = []
    for n in sorted(os.listdir(capdir)):
        if not n.startswith("AX30G_3BEQ_") or not n.endswith(".wav"):
            continue
        if only and only not in n:
            continue
        out.append(os.path.join(capdir, n))
    return out


def _pin_threads():
    for v in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS",
              "VECLIB_MAXIMUM_THREADS", "NUMEXPR_NUM_THREADS"):
        os.environ.setdefault(v, "1")


def row_for(job):
    """One capture: render its own setting through the model, align, fit
    gain, null per segment. Top-level and picklable so it can be a
    ProcessPoolExecutor task (same shape as tests/rev_null.py's row_for).
    Takes and returns only plain data; all heavy imports happen inside so a
    worker that is never used costs nothing.
    """
    (p, spec_path, signal, layout_path, lag_flat, clip_level, clip_position,
     no_pre_emph_domain) = job
    _pin_threads()
    import json as _json
    import numpy as _np
    import soundfile as _sf
    from analysis.util import load_layout as _load_layout, seg as _seg, cut as _cut
    from analysis.run import load_aligned as _load_aligned
    from engine.render import load_spec as _load_spec, render_spec as _render_spec
    from tests.null_test import refine_lag as _refine_lag, frac_shift as _frac_shift, \
        params_from_name as _params_from_name, db as _db

    x, fs = _sf.read(signal, dtype="float64", always_2d=True)
    x = x[:, 0]
    layout = _load_layout(layout_path)
    segs = [s for s in layout["segments"] if s["name"] != "silence"]
    spec0 = _load_spec(spec_path)

    name = os.path.splitext(os.path.basename(p))[0]
    spec = _json.loads(_json.dumps(spec0))
    spec["params"].update(_params_from_name(name))
    if clip_level is not None:
        spec.setdefault("blocks", {}).setdefault("clip", {})["level"] = clip_level
    if clip_position:
        spec.setdefault("blocks", {}).setdefault("clip", {})["position"] = clip_position
    if no_pre_emph_domain:
        st = spec.get("input_stage")
        spec["input_stage"] = st if isinstance(st, dict) else st
        spec["_de_emph_off"] = True
    y, truth = _render_spec(spec, x, fs, spec_path=spec_path)
    cap, _fs = _load_aligned(p, x, fs, layout)
    c = cap[: len(y), 0]
    m = y[: len(c), 0]
    mask = _np.ones(len(c), bool)
    rp = _seg(layout, "ramp")
    mask[int(rp["start"] * fs):int(rp["end"] * fs)] = False
    lag = _refine_lag(c, m, lag_flat, mask, span=1.5)
    m2 = _frac_shift(m, lag)
    g = float(_np.dot(c[mask], m2[mask]) / max(_np.dot(m2[mask], m2[mask]), 1e-20))
    r = c - g * m2
    row = {"capture": name, "params": spec["params"], "lag_samples": lag, "gain_db": _db(abs(g)),
           "segments": {}}
    for s in segs:
        cs, rs = _cut(c, fs, s["start"], s["end"]), _cut(r, fs, s["start"], s["end"])
        row["segments"][s["name"]] = _db(_np.sqrt(_np.mean(rs ** 2))) - _db(_np.sqrt(_np.mean(cs ** 2)))
    row["total"] = _db(_np.sqrt(_np.mean(r[mask] ** 2))) - _db(_np.sqrt(_np.mean(c[mask] ** 2)))
    return row


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", default=os.path.join(ROOT, "models", "ax30g-3beq.json"))
    ap.add_argument("--signal", default=os.path.join(ROOT, "capture", "signalset-normal.wav"))
    ap.add_argument("--layout", default=os.path.join(ROOT, "capture", "layout-normal.json"))
    ap.add_argument("--captures", default=os.path.join(ROOT, "captures"))
    ap.add_argument("--only")
    ap.add_argument("--clip-level", type=float, help="override blocks.clip.level")
    ap.add_argument("--clip-position", choices=["output", "per_band", "none"])
    ap.add_argument("--no-pre-emph-domain", action="store_true",
                    help="run the EQ AFTER the de-emphasis instead (the rejected hypothesis)")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2),
                    help="parallel workers (default cpu_count()-2); 1 = old serial path")
    ap.add_argument("--json", help="write the per-row table here")
    ap.add_argument("--md", help="write a markdown table here")
    a = ap.parse_args()

    x, fs = sf.read(a.signal, dtype="float64", always_2d=True)
    x = x[:, 0]
    layout = load_layout(a.layout)
    paths = rows_from_captures(a.captures, a.only)
    if not paths:
        raise SystemExit("no 3BEQ captures found")

    spec0 = load_spec(a.model)
    spec_path = os.path.abspath(a.model)

    # lag from the flat row, measured on its clicks (a static block, so one
    # number serves every row) -- kept serial, on the main process, exactly
    # as before: it is one render, not worth farming out, and every worker
    # needs its result as an input.
    flat = os.path.join(a.captures, FLAT)
    capf, _ = load_aligned(flat, x, fs, layout)
    specf = json.loads(json.dumps(spec0))
    yf, _ = render_spec(specf, x, fs, spec_path=spec_path)
    sw0 = seg(layout, "sweep")
    maskf = np.ones(min(len(capf), len(yf)), bool)
    rp0 = seg(layout, "ramp")
    maskf[int(rp0["start"] * fs):int(rp0["end"] * fs)] = False
    lag0 = best_shift(cut(capf[:, 0], fs, sw0["start"], sw0["end"]),
                      cut(yf[:, 0], fs, sw0["start"], sw0["end"]), fs, max_ms=2.0)
    lag_flat = refine_lag(capf[: len(maskf), 0], yf[: len(maskf), 0], lag0, maskf, span=1.5)

    jobs = [(p, spec_path, a.signal, a.layout, lag_flat, a.clip_level, a.clip_position,
             a.no_pre_emph_domain) for p in paths]

    if a.jobs > 1 and len(jobs) > 1:
        with ProcessPoolExecutor(max_workers=min(a.jobs, len(jobs))) as ex:
            rows = list(ex.map(row_for, jobs))
    else:
        rows = [row_for(j) for j in jobs]

    for row in rows:
        pr = row["params"]
        print(f"{pr['Bass']:+3.0f} {pr['Mid Freq']:>6.0f} {pr['Mid Gain']:+3.0f} {pr['Treble']:+3.0f} "
              f"{pr['Trim Gain']:+3.0f} | gain {row['gain_db']:+6.2f} lag {row['lag_samples']:+6.3f} | "
              f"sweep {row['segments']['sweep']:+6.1f}  noise {row['segments']['noise']:+6.1f}  "
              f"sine20 {row['segments']['sine20']:+6.1f}  ramp {row['segments']['ramp']:+6.1f}  "
              f"di {row['segments']['di']:+6.1f}  all {row['total']:+6.1f}")

    def med(k):
        return float(np.median([r["segments"][k] for r in rows]))
    print(f"\n{len(rows)} rows.  median null: sweep {med('sweep'):+.1f} dB, noise {med('noise'):+.1f} dB, "
          f"di {med('di'):+.1f} dB, ramp {med('ramp'):+.1f} dB, "
          f"overall {float(np.median([r['total'] for r in rows])):+.1f} dB")
    if a.json:
        with open(a.json, "w") as f:
            json.dump(rows, f, indent=1, default=float)
    if a.md:
        import datetime
        L = [f"# 3BEQ null test against the real captures — {datetime.date.today().isoformat()}",
             "",
             "`tests/eq3_null.py --md <this file>`. Each row renders its own captured setting "
             "through the model (converter chain and input stage included), aligns on one lag "
             "(measured once on the flat row, refined per row on the residual), fits one broadband "
             "gain, and reports residual rms minus capture rms per segment. More negative is better. "
             "The ramp is excluded from the gain fit; its null is still reported.",
             "",
             f"**{len(rows)} rows. Median: sweep {med('sweep'):+.1f} dB, noise {med('noise'):+.1f} dB, "
             f"sine20 {med('sine20'):+.1f} dB, ramp {med('ramp'):+.1f} dB, di {med('di'):+.1f} dB, "
             f"overall {float(np.median([r['total'] for r in rows])):+.1f} dB.**",
             "",
             "| Bass | Mid Freq | Mid Gain | Treble | Trim | gain dB | sweep | noise | sine20 | ramp | di | all |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|"]
        for r in rows:
            p = r["params"]
            L.append("| {:+.0f} | {:.0f} | {:+.0f} | {:+.0f} | {:+.0f} | {:+.2f} | {:+.1f} | {:+.1f} | {:+.1f} | {:+.1f} | {:+.1f} | {:+.1f} |".format(
                p["Bass"], p["Mid Freq"], p["Mid Gain"], p["Treble"], p["Trim Gain"], r["gain_db"],
                r["segments"]["sweep"], r["segments"]["noise"], r["segments"]["sine20"],
                r["segments"]["ramp"], r["segments"]["di"], r["total"]))
        with open(a.md, "w") as f:
            f.write("\n".join(L) + "\n")
        print("wrote", a.md)


if __name__ == "__main__":
    main()
