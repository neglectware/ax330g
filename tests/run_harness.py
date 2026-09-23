#!/usr/bin/env python3
"""Virtual test harness: render every models/sim/*.json through the engine,
run the analysis on the result exactly as it would run on a real capture, and
compare what the analysis recovered with what the spec says. Writes
out/harness-report.md and one PNG per spec in out/.

    .venv/bin/python tests/run_harness.py            # all sims
    .venv/bin/python tests/run_harness.py sdly-clean  # one
"""
import glob
import json
import os
import sys
import time
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from engine import load_spec, render_spec  # noqa: E402
from analysis.util import load_wav, load_layout  # noqa: E402
from analysis.clicks import analyze_clicks, store_is_lti  # noqa: E402
from analysis.storage import analyze_storage  # noqa: E402
from analysis.lfo import analyze_lfo, loop_position, burst_pitch  # noqa: E402
from analysis.differential import damp_from_reference  # noqa: E402
from analysis.fit_by_sim import fit_damp_by_sim, storage_from_verdict, pick_storage_by_images  # noqa: E402
from analysis.report import plot_capture  # noqa: E402
from analysis.align import align_capture  # noqa: E402
import soundfile as sf  # noqa: E402


def check(rows, name, got, want, tol, unit=""):
    if want is None and got is None:
        ok = True
    elif want is None or got is None:
        ok = False
    elif isinstance(want, str):
        ok = got == want
    elif isinstance(want, bool):
        ok = bool(got) == want
    else:
        ok = abs(got - want) <= tol
    rows.append((name, got, want, tol, unit, ok))
    return ok


def fmt(v):
    if v is None:
        return "-"
    if isinstance(v, float):
        return f"{v:.3f}"
    return str(v)


def render_to(spec, name, x, fs):
    y, truth = render_spec(spec, x, fs)
    wav = os.path.join(ROOT, "captures", "sim", name + ".wav")
    os.makedirs(os.path.dirname(wav), exist_ok=True)
    sf.write(wav, y.astype(np.float32), fs, subtype="FLOAT")
    with open(wav[:-4] + ".truth.json", "w") as f:
        json.dump(truth, f, indent=1)
    return wav, truth


def run_one(spec_path, x, fs, layout, outdir):
    name = os.path.basename(spec_path)[:-5]
    spec = load_spec(spec_path)
    t0 = time.time()
    wav, truth = render_to(spec, name, x, fs)
    trender = time.time() - t0
    # analysis, blind to the truth
    cap, _ = load_wav(wav)
    cl = analyze_clicks(cap, fs, layout, x_signal=x)
    st = analyze_storage(cap, fs, layout, cl["delay_ms_median"]) if cl.get("delay_ms_median") else None
    lti = store_is_lti(st)
    if not lti:   # companded / sub-rate store: the sweep IR is not trustworthy, use the click windows
        cl = analyze_clicks(cap, fs, layout, x_signal=x, use_sweep_ir=False)
    rows = []
    st = lf = None
    if spec["effect"] == "SDLY":
        c0 = truth["channels"][0]
        check(rows, "delay_ms", cl.get("delay_ms_median"), c0["delay_ms"], 0.1, "ms")
        check(rows, "fb_coef", cl.get("fb_coef"), c0["fb_coef"], 0.05)
        want_fc = c0["damp_hz"]
        got_fc = cl.get("damp_hz")
        rows.append(("damp_hz_single_capture", got_fc, want_fc, 0, "Hz", True))
        if want_fc is not None:
            # differential against the same spec at High Damp 0 (what the grid does)
            ref = json.loads(json.dumps(spec))
            ref["params"]["High Damp"] = 0
            rwav, _ = render_to(ref, name + "-ref-damp0", x, fs)
            rcap, _ = load_wav(rwav)
            cl_ref = analyze_clicks(rcap, fs, layout, x_signal=x)
            st_ref = analyze_storage(rcap, fs, layout, cl_ref["delay_ms_median"])
            if not store_is_lti(st_ref):
                cl_ref = analyze_clicks(rcap, fs, layout, x_signal=x, use_sweep_ir=False)
                lti = False
            dd = damp_from_reference(cl, cl_ref)   # both sides now share a spectra source
            rows.append(("damp_hz_differential_onepole", dd.get("damp_hz"), want_fc, 0, "Hz", True))
            rows.append(("damp_diff_err_db", dd.get("err_db"), None, 0, "dB", True))
            stors = storage_from_verdict(st_ref)
            ranked = pick_storage_by_images(spec, stors, cl_ref["delay_ms_median"], st_ref, x, fs, layout)
            bs = ranked[0]["storage"]
            want_st = c0["storage"]
            want_str = f"{want_st['bits']}b {want_st['compand']} /{want_st['rate_div']} {want_st.get('prefilter', '-')}/{want_st.get('upsample', '-')}" if want_st['rate_div'] > 1 else f"{want_st['bits']}b {want_st['compand']} /1 -/-"
            got_str = f"{bs['bits']}b {bs['compand']} /{bs['rate_div']} {bs.get('prefilter', '-')}/{bs.get('upsample', '-')}" if bs['rate_div'] > 1 else f"{bs['bits']}b {bs['compand']} /1 -/-"
            check(rows, "storage_variant_by_images", got_str, want_str, 0)
            rows.append(("storage_ranking(img+noise)", [(r['storage'].get('prefilter', '-') + '/' + r['storage'].get('upsample', '-'), round(r['err_images_db'], 1), round(r['err_noise_db'], 1)) for r in ranked], None, 0, "dB", True))
            fit = fit_damp_by_sim(spec, bs, cl_ref["fb_coef"], cl_ref["delay_ms_median"], dd["ratio_db"],
                                  dd["bands_hz"], cl_ref.get("shape_mask", [True] * len(dd["bands_hz"])), x, fs, layout, use_sweep_ir=lti, ratio_key=dd.get("from"))
            check(rows, "damp_hz_by_sim", fit["damp_hz"], want_fc, 0.25 * want_fc, "Hz")
            rows.append(("damp_sim_err_db", fit["err_db"], None, 0, "dB", True))
        if st is not None:
            s = c0["storage"]
            check(rows, "companding", st.get("companding_verdict"),
                  "companded" if s["compand"] != "none" else "linear PCM", 0)
            check(rows, "rate_div", st.get("rate_div"), s["rate_div"], 0)
            rows.append(("floor_drop_db", st.get("floor_drop_db"), None, 0, "dB", True))
            rows.append(("sinad_wet-20", st["sine20"].get("wet_sinad_db"), None, 0, "dB", True))
            rows.append(("sinad_wet-40", st["sine40"].get("wet_sinad_db"), None, 0, "dB", True))
            rows.append(("bw_10db_hz(noise)", st.get("bw_10db_hz"), None, 0, "Hz", True))
            rows.append(("image_peaks", [(round(f), round(l, 1)) for f, l in st.get("image_peaks", [])[:4]], None, 0, "", True))
            rows.append(("loop_fit_err_db", cl.get("loop_fit_err_db"), None, 0, "dB", True))
    elif spec["effect"] == "MODD":
        lf = analyze_lfo(cap, fs, layout)
        check(rows, "lfo_hz", lf["lfo_hz"], truth["lfo_hz"], 0.05, "Hz")
        check(rows, "depth_ms", lf["depth_ms"], truth["depth_ms"], 0.15 * truth["depth_ms"] + 0.05, "ms")
        if truth["fb_coef"] < 0.05:
            check(rows, "lfo_shape", lf["shape"], truth["lfo_shape"], 0)
        else:
            rows.append(("lfo_shape(fb>0, info)", lf["shape"] + f" crest {lf['crest']:.2f}", truth["lfo_shape"], 0, "", True))
        check(rows, "stepped", lf["stepped"], truth["lfo_update_every"] > 1, 0)
        rows.append(("step_ratio", lf["step_ratio"], None, 0, "", True))
        lp = loop_position(cl, lf["depth_ms"])
        check(rows, "nominal_delay_ms", lp.get("nominal_delay_ms"), truth["delay_ms"], 0.5 + truth["depth_ms"], "ms")
        rows.append(("click_offsets_ms", [round(v, 2) for v in lp.get("max_abs_offset_by_repeat_ms", [])], None, 0, "", True))
        if truth["fb_coef"] >= 0.05:
            ref = json.loads(json.dumps(spec))
            ref["params"]["Feedback"] = 0
            rwav, _ = render_to(ref, name + "-ref-fb0", x, fs)
            rcap, _ = load_wav(rwav)
            lf0 = analyze_lfo(rcap, fs, layout)
            rows.append(("peak_slope_fb0", lf0["peak_slope"] * 100, None, 0, "%", True))
            bp = burst_pitch(cap, fs, layout, lp["nominal_delay_ms"], lf0["depth_ms"], lf0["peak_slope"])
            check(rows, "loop_position", bp.get("verdict"), "in_loop" if truth["in_loop"] else "after_loop", 0)
            rows.append(("burst_compounding_ratio", bp.get("compounding_ratio"), None, 0, "", True))
            rows.append(("burst_peak_dev_by_repeat", [round(v * 100, 3) if v else None for v in bp["peak_abs_dev_by_repeat"]], None, 0, "%", True))
    png = os.path.join(outdir, name + ".png")
    plot_capture(name, cap, fs, layout, cl, st, lf, png, truth)
    return name, rows, trender, png


def alignment_test(x, fs, layout):
    """Render one spec, wrap it as a hand-started 3-channel recording with a
    known offset, and check the reference-channel alignment recovers it."""
    spec = load_spec(os.path.join(ROOT, "models", "sim", "sdly-8bit-mu-half.json"))
    spec["params"]["L Bal"] = 50  # all wet: the hard case for any dry-based alignment
    spec["params"]["R Bal"] = 50
    y, _ = render_spec(spec, x, fs)
    rng = np.random.default_rng(3)
    offset = 4.73211
    lead = int(round(offset * fs))
    rec = rng.standard_normal((lead + len(y) + int(2 * fs), 3)) * 1e-4  # -80 dBFS noise
    rec[lead:lead + len(y), :2] += y
    rec[lead:lead + len(x), 2] += x * 10 ** (-12 / 20)
    al, info = align_capture(rec, fs, x, layout)
    rows = []
    check(rows, "offset_s", info["offset_s"], offset, 0.0001, "s")
    check(rows, "ref_gain_db", info["ref_gain_db"], -12.0, 0.1, "dB")
    check(rows, "click_disagreement_ms", abs(info["click_disagreement_ms"]), 0.0, 0.05, "ms")
    cl = analyze_clicks(al, fs, layout, x_signal=x)
    check(rows, "delay_after_align_ms", cl.get("delay_ms_median"), 300.006, 0.1, "ms")
    return "alignment-3ch", rows


def main():
    outdir = os.path.join(ROOT, "out")
    os.makedirs(outdir, exist_ok=True)
    x, fs = load_wav(os.path.join(ROOT, "capture", "signalset.wav"))
    x = x[:, 0]
    layout = load_layout(os.path.join(ROOT, "capture", "layout.json"))
    specs = sorted(glob.glob(os.path.join(ROOT, "models", "sim", "*.json")))
    if len(sys.argv) > 1:
        specs = [s for s in specs if any(a in s for a in sys.argv[1:])]
    lines = ["# Harness report", "", f"signal set: {layout['duration']:.1f} s at {fs} Hz", ""]
    total_ok = True
    if len(sys.argv) == 1 or any(a in "alignment" for a in sys.argv[1:]):
        name, rows = alignment_test(x, fs, layout)
        ok = all(r[5] for r in rows)
        total_ok &= ok
        print(f"\n== {name}  {'PASS' if ok else 'FAIL'}")
        lines += [f"## {name} — {'PASS' if ok else 'FAIL'}", "", "| check | got | want | tol | ok |", "|---|---|---|---|---|"]
        for n, got, want, tol, unit, k in rows:
            print(f"   {n:28s} got {fmt(got):>10s}  want {fmt(want):>10s} {unit:3s} {'ok' if k else '**FAIL**'}")
            lines.append(f"| {n} | {fmt(got)} | {fmt(want)} | {fmt(tol)} {unit} | {'ok' if k else '**FAIL**'} |")
        lines.append("")
    for sp in specs:
        name, rows, tr, png = run_one(sp, x, fs, layout, outdir)
        ok = all(r[5] for r in rows)
        total_ok &= ok
        print(f"\n== {name}  ({tr:.1f} s render)  {'PASS' if ok else 'FAIL'}")
        lines += [f"## {name} — {'PASS' if ok else 'FAIL'}", "", f"![]({os.path.basename(png)})", "",
                  "| check | got | want | tol | ok |", "|---|---|---|---|---|"]
        for n, got, want, tol, unit, k in rows:
            flag = "ok" if k else "**FAIL**"
            if want is None and tol == 0 and k and n not in ("damp_hz",):
                flag = "info"
            print(f"   {n:28s} got {fmt(got):>10s}  want {fmt(want):>10s} {unit:3s} {flag}")
            lines.append(f"| {n} | {fmt(got)} | {fmt(want)} | {fmt(tol)} {unit} | {flag} |")
        lines.append("")
    with open(os.path.join(outdir, "harness-report.md"), "w") as f:
        f.write("\n".join(lines))
    print(f"\n{'ALL PASS' if total_ok else 'SOME FAILURES'} -> out/harness-report.md")
    sys.exit(0 if total_ok else 1)


if __name__ == "__main__":
    main()
