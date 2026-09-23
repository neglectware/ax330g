#!/usr/bin/env python3
"""Null every REV capture against a REV model spec.

    .venv/bin/python tests/rev_null.py [--model models/ax30g-rev.json]
                                       [--only HALL] [--set rev|tail|both]
                                       [--segments clicks,sweep,burst_hot]
                                       [--jobs N]

Both REV capture sets are handled: the TAIL set (`capture/layout-tail.json`:
clicks / bursts / burst_hot / sine20) and the hot-broadband REV set
(`capture/layout-rev.json`: clicks / sweep / burst_hot), chosen per capture
from its `SET-...` name.

For each capture the matching signal set is rendered through the model (device
rate + the measured converter chain), aligned on the capture with one
fractional lag and one gain per channel, and the residual reported per
segment. The lag/gain are fitted on the segment being reported, so a number
here is the best case for that model: it is a shape comparison, not a level
check.

Captures are independent, so they are farmed out to a process pool
(`--jobs`, default `cpu_count() - 2`); each worker pins BLAS/FFT to one
thread so the pool is not oversubscribed. `--jobs 1` runs the old serial
path and produces byte-identical output (the work per capture is
deterministic and shares no state).
"""
import argparse
import os
import re
import sys
import numpy as np
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

# the aborted ROOM Rev Time 10 capture (41 s, the unit was probably not yet
# set); its `_take2` retake is the good one -- see docs/rev-model-2026-09-18.md
SKIP = {"AX30G_REV_HighDamp-0_Balance-50_Type-ROOM_PreDly-1_RevTime-10_IN-LIN_SET-rev.wav"}

PAT = re.compile(r"AX30G_REV_HighDamp-(?P<hd>[\d.]+)_Balance-(?P<bal>[\d.]+)_Type-(?P<type>\w+)"
                 r"_PreDly-(?P<pd>[\d.]+)_RevTime-(?P<rt>[\d.]+)_IN-LIN_SET-(?P<set>\w+?)(?:_take(?P<take>\d+))?\.wav")


def params_from_name(name):
    m = PAT.match(os.path.basename(name))
    if not m:
        return None, None
    d = m.groupdict()
    return {"Type": d["type"], "Pre Dly": float(d["pd"]), "Rev Time": float(d["rt"]),
            "High Damp": float(d["hd"]), "Balance": float(d["bal"])}, d["set"]


def _pin_threads():
    for v in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS",
              "VECLIB_MAXIMUM_THREADS", "NUMEXPR_NUM_THREADS"):
        os.environ.setdefault(v, "1")


def row_for(job):
    """One capture: render the model at its settings, null it per segment.

    Top-level and picklable so it can be a ProcessPoolExecutor task. Takes and
    returns only plain data; all heavy imports happen inside so a worker that
    is never used costs nothing.
    """
    model, f, want, skip_s, limit_s, chans = job
    _pin_threads()
    import analysis.rev2 as R2
    from analysis.util import seg
    from engine.render import load_spec, render_spec

    spec = load_spec(model)
    p, which = params_from_name(f)
    x, fs_sig = R2.signal(which)
    lay = R2.layout(which)
    names = [s["name"] for s in lay["segments"] if s["name"] in want]
    spec["params"] = p
    y, _ = render_spec(spec, x, fs_sig, spec_path=model)
    cap, fs, info, _ = R2.load(f)
    out = {}
    for s in names:
        sg = seg(lay, s)
        i0 = int((sg["start"] + skip_s) * fs)
        i1 = int(sg["end"] * fs) if limit_s is None else int((sg["start"] + skip_s + limit_s) * fs)
        i1 = min(i1, cap.shape[0], y.shape[0])
        vals = [R2.null_fast(cap[i0:i1, c], y[i0:i1, c])[0] for c in chans]
        out[s] = float(np.mean(vals))
    label = re.sub(r"_IN-LIN_SET-", " ", f.replace("AX30G_REV_", "").replace(".wav", ""))
    return label, out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", default=os.path.join(ROOT, "models", "ax30g-rev.json"))
    ap.add_argument("--only", help="only captures whose name contains this")
    ap.add_argument("--set", default="both", choices=("rev", "tail", "both"))
    ap.add_argument("--segments", default="clicks,bursts,burst_hot,sine20,sweep")
    ap.add_argument("--skip-s", type=float, default=0.0)
    ap.add_argument("--limit-s", type=float, default=None)
    ap.add_argument("--channels", default="0,1")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    a = ap.parse_args()

    import analysis.rev2 as R2
    want = a.segments.split(",")
    chans = [int(c) for c in a.channels.split(",")]
    caps = sorted(f for f in os.listdir(R2.CAP_DIR)
                  if PAT.match(f) and f not in SKIP and (not a.only or a.only in f))
    caps = [f for f in caps if a.set == "both" or f"SET-{a.set}" in f]
    jobs = [(a.model, f, want, a.skip_s, a.limit_s, chans) for f in caps]

    if a.jobs > 1 and len(jobs) > 1:
        with ProcessPoolExecutor(max_workers=min(a.jobs, len(jobs))) as ex:
            results = list(ex.map(row_for, jobs))
    else:
        results = [row_for(j) for j in jobs]

    labels = [r[0] for r in results]
    rows = [r[1] for r in results]
    allseg = []
    for r in rows:
        for k in r:
            if k not in allseg:
                allseg.append(k)
    print(f"{'capture':62s} " + " ".join(f"{s:>10s}" for s in allseg))
    for lab, r in zip(labels, rows):
        print(f"{lab:62s} " + " ".join(f"{r[s]:10.2f}" if s in r else f"{'':>10s}" for s in allseg))
    for stat, fn in (("median", np.median), ("best", np.min), ("worst", np.max)):
        vals = []
        for s in allseg:
            v = [r[s] for r in rows if s in r]
            vals.append(fn(v) if v else float("nan"))
        print(f"{stat:62s} " + " ".join(f"{v:10.2f}" for v in vals))


if __name__ == "__main__":
    main()
