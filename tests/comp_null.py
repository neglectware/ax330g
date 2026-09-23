#!/usr/bin/env python3
"""COMP grid null test: render every captured COMP setting through the model
and null it against the real capture, one row per capture, per segment.

    tests/comp_null.py
    tests/comp_null.py --model models/ax30g-comp.json
    tests/comp_null.py --only Sensitivity-50
    tests/comp_null.py --jobs 1          # serial, identical output
    tests/comp_null.py --md docs/comp-null-2026-09-18.md

Method (the same shape as tests/eq3_null.py and tests/rev_null.py).  Each row
renders its own captured Sensitivity/Level/Attack through
`models/ax30g-comp.json` -- converter chain, input stage and the
pre-emphasised domain included -- aligns it to the capture (cross-correlation
on the sweep, then refined on the residual over a +-1.5 sample window), fits
ONE broadband gain (the unit's own fixed output gain, about -6 dB, is not part
of the COMP model), and reports residual rms minus capture rms per segment.
More negative is better.

The ramp is excluded from the gain fit for the same reason the 3BEQ test
excludes it: it is the one segment that reaches the converter's ceiling, and a
gain fitted through it would flatter everything else.  Its null is still
reported.

The two Level-0 captures are skipped by default: Level 0 is a MUTE (both sit
at -104 dBFS on the -20 dBFS sine, i.e. the unit's own noise floor), so the
model renders digital silence, the residual IS the capture and the null is
0.00 dB by construction -- a number that says nothing.  `--include-mute`
reports them anyway, with the capture's own floor level, which is the only
useful thing there is to say about them.
"""
import argparse
import json
import os
import sys
import numpy as np
import soundfile as sf
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
from analysis.util import load_layout, seg, cut                      # noqa: E402
from analysis.run import load_aligned                                # noqa: E402
from engine.render import load_spec, render_spec                     # noqa: E402
import engine.comp                                                   # noqa: E402,F401  (registers COMP)
from tests.null_test import best_shift, refine_lag, frac_shift, db    # noqa: E402

SEGMENTS = ("clicks", "bursts", "sine20", "sine40", "ramp", "sweep", "noise", "di")


def params_from_name(name):
    import re
    out = {}
    for k, v in re.findall(r"(Sensitivity|Level|Attack)-(-?\d+)", os.path.basename(name)):
        out[k] = int(v)
    return out


def rows_from_captures(capdir, only=None, include_mute=False):
    out = []
    for n in sorted(os.listdir(capdir)):
        if not n.startswith("AX30G_COMP_") or not n.endswith(".wav"):
            continue
        if only and only not in n:
            continue
        if not include_mute and params_from_name(n).get("Level", 1) == 0:
            continue
        out.append(os.path.join(capdir, n))
    return out


def _pin_threads():
    for v in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS",
              "VECLIB_MAXIMUM_THREADS", "NUMEXPR_NUM_THREADS"):
        os.environ.setdefault(v, "1")


def row_for(job):
    """One capture, start to finish.  Top-level and picklable so it can be a
    ProcessPoolExecutor task; takes and returns only plain data, and does its
    heavy imports inside so an unused worker costs nothing."""
    p, spec_path, signal, layout_path, overrides = job
    _pin_threads()
    import json as _json
    import numpy as _np
    import soundfile as _sf
    from analysis.util import load_layout as _load_layout, seg as _seg, cut as _cut
    from analysis.run import load_aligned as _load_aligned
    from engine.render import load_spec as _load_spec, render_spec as _render_spec
    import engine.comp                                              # noqa: F401
    from tests.null_test import best_shift as _best, refine_lag as _refine, \
        frac_shift as _shift, db as _db

    x, fs = _sf.read(signal, dtype="float64", always_2d=True)
    x = x[:, 0]
    layout = _load_layout(layout_path)
    segs = [s for s in layout["segments"] if s["name"] != "silence"]

    name = os.path.splitext(os.path.basename(p))[0]
    spec = _json.loads(_json.dumps(_load_spec(spec_path)))
    spec["params"].update(params_from_name(name))
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

    if float(_np.max(_np.abs(m))) <= 0.0:          # a mute row
        row = {"capture": name, "params": spec["params"], "mute": True,
               "lag_samples": 0.0, "gain_db": float("-inf"),
               "capture_floor_dbfs": _db(_np.sqrt(_np.mean(c ** 2))),
               "segments": {s["name"]: 0.0 for s in segs}, "total": 0.0}
        return row

    sw = _seg(layout, "sweep")
    lag0 = _best(_cut(c, fs, sw["start"], sw["end"]), _cut(m, fs, sw["start"], sw["end"]),
                 fs, max_ms=2.0)
    lag = _refine(c, m, lag0, mask, span=1.5)
    m2 = _shift(m, lag)
    g = float(_np.dot(c[mask], m2[mask]) / max(_np.dot(m2[mask], m2[mask]), 1e-20))
    r = c - g * m2

    row = {"capture": name, "params": spec["params"], "lag_samples": lag,
           "gain_db": _db(abs(g)), "mute": False,
           "a": truth.get("a"), "b": truth.get("b"),
           "max_gain_db": truth.get("max_gain_db"), "ceiling_dbfs": truth.get("ceiling_dbfs"),
           "segments": {}}
    for s in segs:
        cs = _cut(c, fs, s["start"], s["end"])
        rs = _cut(r, fs, s["start"], s["end"])
        row["segments"][s["name"]] = _db(_np.sqrt(_np.mean(rs ** 2))) - _db(_np.sqrt(_np.mean(cs ** 2)))
    row["total"] = _db(_np.sqrt(_np.mean(r[mask] ** 2))) - _db(_np.sqrt(_np.mean(c[mask] ** 2)))
    return row


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", default=os.path.join(ROOT, "models", "ax30g-comp.json"))
    ap.add_argument("--signal", default=os.path.join(ROOT, "capture", "signalset-normal.wav"))
    ap.add_argument("--layout", default=os.path.join(ROOT, "capture", "layout-normal.json"))
    ap.add_argument("--captures", default=os.path.join(ROOT, "captures"))
    ap.add_argument("--only")
    ap.add_argument("--include-mute", action="store_true",
                    help="also report the two Level-0 (mute) captures")
    ap.add_argument("--release-tau", type=float, help="override blocks.detector.release_tau_ms")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    ap.add_argument("--json")
    ap.add_argument("--md")
    a = ap.parse_args()

    paths = rows_from_captures(a.captures, a.only, a.include_mute)
    if not paths:
        raise SystemExit("no COMP captures found")
    spec_path = os.path.abspath(a.model)
    ov = {}
    if a.release_tau is not None:
        base = load_spec(spec_path)
        det = dict(base.get("blocks", {}).get("detector", {}))
        det["release_tau_ms"] = a.release_tau
        ov["blocks.detector"] = det

    jobs = [(p, spec_path, a.signal, a.layout, ov) for p in paths]
    if a.jobs > 1 and len(jobs) > 1:
        with ProcessPoolExecutor(max_workers=min(a.jobs, len(jobs))) as ex:
            rows = list(ex.map(row_for, jobs))
    else:
        rows = [row_for(j) for j in jobs]

    for row in rows:
        p = row["params"]
        head = f"S{p['Sensitivity']:>2} L{p['Level']:>2} A{p['Attack']:>2}"
        if row.get("mute"):
            print(f"{head} | MUTE (model renders silence; capture floor "
                  f"{row['capture_floor_dbfs']:+.1f} dBFS)")
            continue
        s = row["segments"]
        print(f"{head} | gain {row['gain_db']:+6.2f} lag {row['lag_samples']:+6.3f} | "
              f"ramp {s['ramp']:+6.1f}  sine20 {s['sine20']:+6.1f}  sine40 {s['sine40']:+6.1f}  "
              f"bursts {s['bursts']:+6.1f}  clicks {s['clicks']:+6.1f}  sweep {s['sweep']:+6.1f}  "
              f"noise {s['noise']:+6.1f}  di {s['di']:+6.1f}  all {row['total']:+6.1f}")

    live = [r for r in rows if not r.get("mute")]

    def med(k):
        return float(np.median([r["segments"][k] for r in live]))
    if live:
        print(f"\n{len(live)} rows.  median null: ramp {med('ramp'):+.1f} dB, "
              f"sine20 {med('sine20'):+.1f} dB, sine40 {med('sine40'):+.1f} dB, "
              f"bursts {med('bursts'):+.1f} dB, di {med('di'):+.1f} dB, "
              f"overall {float(np.median([r['total'] for r in live])):+.1f} dB")
    if a.json:
        with open(a.json, "w") as f:
            json.dump(rows, f, indent=1, default=float)
    if a.md:
        import datetime
        L = [f"# COMP null test against the real captures — {datetime.date.today().isoformat()}", "",
             "`tests/comp_null.py --md <this file>`. Each row renders its own captured setting "
             "through `models/ax30g-comp.json` (converter chain, input stage and the pre-emphasised "
             "domain included), aligns on the sweep and refines the lag on the residual, fits one "
             "broadband gain, and reports residual rms minus capture rms per segment. More negative "
             "is better. The ramp is excluded from the gain fit; its null is still reported.",
             ("The two Level-0 captures are mutes: the model renders digital silence, so the residual "
              "IS the capture and the null is 0.00 dB by construction. They are listed with the "
              "capture's own floor, which is the only useful thing there is to say about them."
              if any(r.get("mute") for r in rows) else
              "The two Level-0 captures are mutes and are not listed."), ""]
        if live:
            L += [f"**{len(live)} rows. Median: ramp {med('ramp'):+.1f} dB, sine20 {med('sine20'):+.1f} dB, "
                  f"sine40 {med('sine40'):+.1f} dB, bursts {med('bursts'):+.1f} dB, "
                  f"clicks {med('clicks'):+.1f} dB, sweep {med('sweep'):+.1f} dB, "
                  f"noise {med('noise'):+.1f} dB, di {med('di'):+.1f} dB, "
                  f"overall {float(np.median([r['total'] for r in live])):+.1f} dB.**", ""]
        L += ["| Sens | Level | Attack | gain dB | lag | ramp | sine20 | sine40 | bursts | clicks | sweep | noise | di | all |",
              "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
        for r in rows:
            p = r["params"]
            if r.get("mute"):
                L.append(f"| {p['Sensitivity']} | {p['Level']} | {p['Attack']} | "
                         f"mute — model renders silence, capture floor {r['capture_floor_dbfs']:+.1f} dBFS "
                         "| | | | | | | | | |")
                continue
            s = r["segments"]
            L.append("| {} | {} | {} | {:+.2f} | {:+.2f} | {:+.1f} | {:+.1f} | {:+.1f} | {:+.1f} | {:+.1f} | {:+.1f} | {:+.1f} | {:+.1f} | {:+.1f} |".format(
                p["Sensitivity"], p["Level"], p["Attack"], r["gain_db"], r["lag_samples"],
                s["ramp"], s["sine20"], s["sine40"], s["bursts"], s["clicks"], s["sweep"],
                s["noise"], s["di"], r["total"]))
        with open(a.md, "w") as f:
            f.write("\n".join(L) + "\n")
        print("wrote", a.md)


if __name__ == "__main__":
    main()
