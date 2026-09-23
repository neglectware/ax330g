"""Null test for the delay Ducking model (docs/ducking-model-2026-09-18.md).

Renders each of the seven `capture/grids/sdly-ducking.json` captures through
`models/ax30g-sdly.json` + `models/ax30g-ducking.json` and reports, per
segment, how far the residual sits below the capture -- with the ducker on
and, for comparison, with it off (which is what the SDLY model alone does).

    tests/ducking_null.py                       # all seven, both ways
    tests/ducking_null.py --only 50             # just the Ducking-50 rows
    tests/ducking_null.py --scan k 0.36 0.38 9  # scan one ducking parameter
    tests/ducking_null.py --scan attack_ms 30 90 7

Alignment, lag refinement and the single per-channel gain are exactly
tests/null_test.py's (imported from it, not copied). The ducker's own
parameters come from models/ax30g-ducking.json unless overridden.
"""
import argparse
import json
import os
import re
import sys
from concurrent.futures import ProcessPoolExecutor

import numpy as np
import soundfile as sf
from scipy.signal import fftconvolve

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
from analysis.util import load_layout, seg, cut                     # noqa: E402
from analysis.run import load_aligned                               # noqa: E402
from engine.render import load_spec, render_spec                    # noqa: E402
from engine.ducking import load_ducking, render_spec_ducking        # noqa: E402
from tests.null_test import best_shift, refine_lag, frac_shift, db, params_from_name  # noqa: E402

DUCK_RE = re.compile(r"Ducking-(-?\d+)")
CAPDIR = os.path.join(ROOT, "captures")
NAME = ("AX30G_SDLY_LDly-300_RDly-300_LFb-{fb}_RFb-{fb}_HighDamp-0"
        "_LBal-25_RBal-25_Ducking-{d}_IN-LIN.wav")
GRID = [(25, 0), (25, 10), (25, 25), (25, 40), (25, 50), (40, 0), (40, 50)]


def ducking_from_name(name):
    m = DUCK_RE.search(name)
    return float(m.group(1)) if m else 0.0


def run_one(args):
    """(capture path, duck on/off, ducking-parameter overrides) -> row dict."""
    path, use_duck, over, eq = args
    x, fs = sf.read(os.path.join(ROOT, "capture", "signalset.wav"),
                    dtype="float64", always_2d=True)
    x = x[:, 0]
    layout = load_layout(os.path.join(ROOT, "capture", "layout.json"))
    name = os.path.splitext(os.path.basename(path))[0]
    cap, _ = load_aligned(path, x, fs, layout)

    spec_path = os.path.join(ROOT, "models", "ax30g-sdly.json")
    spec = load_spec(spec_path)
    spec["params"].update(params_from_name(name))
    dv = ducking_from_name(name)
    spec["params"]["Ducking"] = dv

    if use_duck and dv:
        d = load_ducking({"ducking": "ax30g-ducking.json"}, spec_path)
        for k, v in (over or {}).items():
            if k in ("peak_attack_ms", "peak_release_ms"):
                d["detector"].setdefault("peak", {})[k[5:]] = v
            elif k in ("attack_ms", "release_ms", "rectifier"):
                d["detector"][k] = v
            else:
                d["gain"][k] = v
        spec["ducking"] = d
        y, _ = render_spec_ducking(spec, x, fs, spec_path=spec_path)
    else:
        # since 2026-09-18 render_spec applies the ducker itself when the
        # model names one, so the "ducker off" row has to switch it off
        # explicitly -- that row is the before-picture, the plain SDLY model.
        spec["ducking"] = None
        y, _ = render_spec(spec, x, fs, spec_path=spec_path)
    if eq is not None:
        h = np.load(eq)
        y = np.stack([fftconvolve(y[:, c], h, mode="full")[: y.shape[0]] for c in range(2)], axis=1)

    segs = [s for s in layout["segments"] if s["name"] != "silence"]
    out = {"capture": name, "ducking": dv, "duck": bool(use_duck and dv), "channels": []}
    for ch, side in enumerate("LR"):
        c = cap[: len(y), ch]
        m = y[: len(c), ch]
        sw = seg(layout, "sweep")
        lag0 = best_shift(cut(c, fs, sw["start"], sw["end"]), cut(m, fs, sw["start"], sw["end"]), fs)
        mask = np.ones(len(c), bool)
        rp = seg(layout, "ramp")
        mask[int(rp["start"] * fs):int(rp["end"] * fs)] = False
        lag = refine_lag(c, m, lag0, mask)
        m2 = frac_shift(m, lag)
        g = float(np.dot(c[mask], m2[mask]) / max(np.dot(m2[mask], m2[mask]), 1e-20))
        r = c - g * m2
        rows = []
        for s in segs:
            cs, rs = cut(c, fs, s["start"], s["end"]), cut(r, fs, s["start"], s["end"])
            rows.append({"name": s["name"],
                         "capture_dbfs": db(np.sqrt(np.mean(cs ** 2))),
                         "null_db": db(np.sqrt(np.mean(rs ** 2))) - db(np.sqrt(np.mean(cs ** 2)))})
        out["channels"].append({
            "side": side, "lag_samples": lag, "gain_db": db(abs(g)), "segments": rows,
            "null_db_total": db(np.sqrt(np.mean(r[mask] ** 2))) - db(np.sqrt(np.mean(c[mask] ** 2)))})
    return out


def table(rows, segnames):
    hdr = "%-4s %-5s %-6s " % ("Fb", "Duck", "ducker") + " ".join("%8s" % s[:8] for s in segnames) + "   overall"
    print(hdr)
    print("-" * len(hdr))
    for fb, dv, on, row in rows:
        vals = {s["name"]: s["null_db"] for s in row["channels"][0]["segments"]}
        print("%-4d %-5d %-6s " % (fb, dv, "on" if on else "off")
              + " ".join("%8.1f" % vals.get(s, float("nan")) for s in segnames)
              + "   %7.1f" % row["channels"][0]["null_db_total"])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", type=int, nargs="*", help="only these Ducking values")
    ap.add_argument("--eq", default=os.path.join(ROOT, "out", "null", "chain-loop.npy"),
                    help="measurement-chain FIR (the Scarlett loopback), as null_test.py's --eq")
    ap.add_argument("--no-eq", action="store_true")
    ap.add_argument("--scan", nargs=4, metavar=("PARAM", "LO", "HI", "N"),
                    help="scan one ducking parameter (k, min_gain, attack_ms, release_ms, "
                         "peak_attack_ms, peak_release_ms)")
    ap.add_argument("--set", nargs="*", default=[], metavar="K=V",
                    help="override ducking parameters for every render")
    ap.add_argument("--jobs", type=int, default=min(12, os.cpu_count() or 4))
    ap.add_argument("--out", default=os.path.join(ROOT, "out", "ducking"))
    a = ap.parse_args()

    eq = None if a.no_eq else (a.eq if os.path.exists(a.eq) else None)
    over = {}
    for kv in a.set:
        k, v = kv.split("=", 1)
        over[k] = v if k == "rectifier" else float(v)

    grid = [(fb, d) for fb, d in GRID if not a.only or d in a.only]
    paths = [(fb, d, os.path.join(CAPDIR, NAME.format(fb=fb, d=d))) for fb, d in grid]
    missing = [p for _, _, p in paths if not os.path.exists(p)]
    if missing:
        sys.exit("missing captures:\n  " + "\n  ".join(missing))
    os.makedirs(a.out, exist_ok=True)

    if a.scan:
        pname, lo, hi, n = a.scan[0], float(a.scan[1]), float(a.scan[2]), int(a.scan[3])
        vals = np.linspace(lo, hi, n)
        jobs = []
        for v in vals:
            o = dict(over)
            o[pname] = float(v)
            for fb, d, p in paths:
                if d:
                    jobs.append((p, True, o, eq))
        with ProcessPoolExecutor(a.jobs) as ex:
            res = list(ex.map(run_one, jobs))
        per = len([1 for _, d, _ in paths if d])
        print("scan %s: mean overall null (L), dB -- lower is better" % pname)
        best = None
        for i, v in enumerate(vals):
            chunk = res[i * per:(i + 1) * per]
            tot = float(np.mean([r["channels"][0]["null_db_total"] for r in chunk]))
            det = " ".join("%d:%5.1f" % (r["ducking"], r["channels"][0]["null_db_total"]) for r in chunk)
            print("  %-10s %8.4f   mean %7.2f   %s" % (pname, v, tot, det))
            if best is None or tot < best[1]:
                best = (v, tot)
        print("best %s = %.4f (mean %.2f dB)" % (pname, best[0], best[1]))
        return

    jobs = [(p, on, over, eq) for fb, d, p in paths for on in ((False, True) if d else (False,))]
    with ProcessPoolExecutor(a.jobs) as ex:
        res = list(ex.map(run_one, jobs))
    segnames = [s["name"] for s in res[0]["channels"][0]["segments"]]
    rows = []
    for r in res:
        fb = 40 if "LFb-40" in r["capture"] else 25
        rows.append((fb, int(r["ducking"]), r["duck"], r))
    rows.sort(key=lambda t: (t[0], t[1], t[2]))
    table(rows, segnames)
    with open(os.path.join(a.out, "ducking-null.json"), "w") as f:
        json.dump([r for _, _, _, r in rows], f, indent=1, default=float)
    print("\nwrote", os.path.join(a.out, "ducking-null.json"))


if __name__ == "__main__":
    main()
