#!/usr/bin/env python3
"""Analyze one capture (what the app calls after each recording).

    run.py CAPTURE.wav --effect SDLY [--ref-damp0 OTHER.wav] [--ref-fb0 OTHER.wav] [--out DIR]

Aligns the capture with the reference channel if it has one, runs the
analysis for the effect, writes <name>.analysis.json and <name>.png in --out
(default: next to the capture), and prints a short summary.
"""
import argparse
import json
import os
import sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
from analysis.util import load_wav, load_layout  # noqa: E402
from analysis.align import align_capture  # noqa: E402
from analysis.clicks import analyze_clicks, store_is_lti  # noqa: E402
from analysis.storage import analyze_storage  # noqa: E402
from analysis.lfo import analyze_lfo, loop_position, burst_pitch  # noqa: E402
from analysis.differential import damp_from_reference  # noqa: E402
from analysis.report import plot_capture  # noqa: E402


FLAT_3BEQ = "AX30G_3BEQ_Bass-0_MidFreq-1000_MidGain-0_Treble-0_Trim-0_IN-LIN.wav"


def _flat_row(d):
    """The 3BEQ grid's flat reference capture, if it is sitting there."""
    p = os.path.join(d, FLAT_3BEQ)
    return p if os.path.exists(p) else None


def load_aligned(path, x_signal, fs_sig, layout):
    cap, fs = load_wav(path)
    if fs != fs_sig:
        # the bench records at whatever rate the interface is set to
        from fractions import Fraction
        from scipy.signal import resample_poly
        fr = Fraction(int(fs_sig), int(fs))
        cap = resample_poly(cap, fr.numerator, fr.denominator, axis=0)
        fs = fs_sig
    if cap.shape[1] >= 3:
        return align_capture(cap, fs, x_signal, layout)
    return cap[:, :2] if cap.shape[1] >= 2 else np.repeat(cap, 2, axis=1), {"method": "none", "offset_s": 0.0}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture")
    ap.add_argument("--effect", required=True, choices=["SDLY", "MODD", "SMOD", "XDLY", "HDLY", "CHO", "FLAN", "BYPASS", "3BEQ", "REV", "COMP", "HYPR"])
    ap.add_argument("--signal", default=os.path.join(ROOT, "capture", "signalset.wav"))
    ap.add_argument("--layout", default=os.path.join(ROOT, "capture", "layout.json"))
    ap.add_argument("--ref-damp0", help="same settings at High Damp 0 (for the differential damp fit)")
    ap.add_argument("--ref-fb0", help="same Speed/Depth at Feedback 0 (for the loop-position test)")
    ap.add_argument("--out")
    ap.add_argument("--no-align", action="store_true")
    ap.add_argument("--hypr-ref", help="HYPR: the capture every row is compared with. Default: the LIN "
                                       "bypass capture. The grid's own first row is NOT usable -- at Type 1 / "
                                       "Harmonics 0 the effect path is 50 dB down.")
    ap.add_argument("--comp-ref", help="COMP: the capture every row is divided by. Default: the LIN bypass "
                                       "capture (captures/AX30G_BYPASS_IN-LIN.wav). The COMP grid's all-zero "
                                       "row is NOT usable as the reference -- Level 0 is a mute.")
    ap.add_argument("--eq-ref", help="3BEQ: the flat row of the grid (all bands 0, Trim 0) that every "
                                     "other capture is divided by. Default: the flat row found beside the "
                                     "capture, else the LIN bypass capture.")
    ap.add_argument("--captures-dir", dest="captures_dir",
                    help="3BEQ: where to look for the flat row (default: beside the capture)")
    a = ap.parse_args()

    x, fs = load_wav(a.signal)
    x = x[:, 0]
    layout = load_layout(a.layout)
    name = os.path.splitext(os.path.basename(a.capture))[0]
    outdir = a.out or os.path.dirname(os.path.abspath(a.capture))
    os.makedirs(outdir, exist_ok=True)

    if a.effect == "REV":
        # a reverb: the click/LFO/storage analyses have nothing to say about
        # it. What the bench wants after a REV capture is the decay -- the
        # Schroeder curve of each tail and the per-octave RT60 of the click.
        # The TAIL signal set is assumed (capture/grids/rev.json).
        from analysis.rev import analyze_rev
        r = analyze_rev(a.capture, outdir=outdir)
        res = {"capture": a.capture, "effect": a.effect, "align": r["align"],
               "rev": {k: v for k, v in r.items() if k != "align"}}
        with open(os.path.join(outdir, name + ".analysis.json"), "w") as f:
            json.dump(res, f, indent=1, default=float)
        bits = [f"align {r['align'].get('method')} offset {r['align'].get('offset_s', 0):.4f} s"]
        if "ref_gain_db" in r["align"]:
            bits.append(f"send {r['align']['ref_gain_db']:+.1f} dB")
        for s_ in ("clicks", "bursts", "burst_hot", "sine20"):
            if s_ in r["segments"]:
                q = r["segments"][s_]
                bits.append(f"{s_} RT60 {q['rt60_s']:.2f} s (r2 {q['r2']:.3f})")
        b = r["click_band_rt60"]
        bits.append("bands " + " ".join(f"{k//1000 if k>=1000 else k}{'k' if k>=1000 else ''}:{v:.2f}" for k, v in b.items()))
        print(" | ".join(bits))
        print(r["png"])
        return

    if a.effect == "COMP":
        # a compressor: the clicks/LFO/storage analyses have nothing to say
        # about it. Its measurement is the static input/output curve from the
        # 5 s ramp plus the attack trajectory inside the bursts, which is
        # analysis/comp_curve.py's job. The reference is the LIN BYPASS
        # capture, never the grid's all-zero row -- Level 0 is a mute, so that
        # row is silence (measured 2026-09-18, docs/comp-model-2026-09-18.md).
        from analysis.comp_curve import analyze_one as comp_one, DEFAULT_BYPASS as COMP_BYPASS, \
            DEFAULT_SIGNAL as COMP_SIGNAL, DEFAULT_LAYOUT as COMP_LAYOUT
        sig = a.signal if a.signal != os.path.join(ROOT, "capture", "signalset.wav") else COMP_SIGNAL
        layp = a.layout if a.layout != os.path.join(ROOT, "capture", "layout.json") else COMP_LAYOUT
        ref = a.comp_ref or a.eq_ref or COMP_BYPASS
        r = comp_one(a.capture, ref, sig, layp, outdir=outdir)
        res = {"capture": a.capture, "effect": a.effect, "align": r["align"],
               "reference": ref, "comp": {k: v for k, v in r.items()
                                          if k not in ("align", "capture", "reference")}}
        with open(os.path.join(outdir, name + ".analysis.json"), "w") as f:
            json.dump(res, f, indent=1, default=float)
        st = r["static"]
        bits = [f"ref {os.path.basename(ref)}",
                f"send {r['align']['send_offset_db']:+.2f} dB"]
        if st:
            bits.append(f"T {st['threshold_dbfs']:.1f} dBFS R {st['ratio']:.1f} knee {st['knee_width_db']:.0f} dB "
                        f"(resid {st['residual_rms_db']:.3f} dB rms)")
            bits.append(f"max GR {st['max_gain_reduction_db']:.1f} dB")
        else:
            bits.append("** static fit failed: Level 0 is a mute **")
        bits.append(f"gain -20 {r['steady']['sine20_gain_db']:+.2f} dB, -40 {r['steady']['sine40_gain_db']:+.2f} dB")
        if r["attack"].get("attack_tau_ms_median"):
            bits.append(f"attack tau {r['attack']['attack_tau_ms_median']:.2f} ms")
        if r["clicks"].get("median_db") is not None:
            bits.append(f"click {r['clicks']['median_db']:+.2f} dB")
        print(" | ".join(bits))
        print(r["png"] or r["csv"])
        return

    if a.effect == "HYPR":
        # a fuzz plus a swept resonator: the click/LFO/storage analyses have
        # nothing to say about it. What the bench wants after a HYPR capture
        # is the transfer curve of the driver (the 1 kHz component of the
        # output through the 5 s ramp) and a spectrogram of the DI clip with
        # the resonator's peak track on it -- analysis/hypr.py's job. The
        # reference is the LIN BYPASS capture, never the grid's own row 1
        # (measured 2026-09-18, docs/hypr-model-2026-09-18.md).
        from analysis.hypr import analyze_one as hypr_one, DEFAULT_BYPASS as HY_BYPASS, \
            DEFAULT_SIGNAL as HY_SIGNAL, DEFAULT_LAYOUT as HY_LAYOUT
        sig = a.signal if a.signal != os.path.join(ROOT, "capture", "signalset.wav") else HY_SIGNAL
        layp = a.layout if a.layout != os.path.join(ROOT, "capture", "layout.json") else HY_LAYOUT
        ref = a.hypr_ref or HY_BYPASS
        r = hypr_one(a.capture, ref, sig, layp, outdir=outdir)
        res = {"capture": a.capture, "effect": a.effect, "align": r["align"],
               "reference": ref, "hypr": {k: v for k, v in r.items() if k != "align"}}
        with open(os.path.join(outdir, name + ".analysis.json"), "w") as f:
            json.dump(res, f, indent=1, default=float)
        h = r["harmonics_sine20_dbfs"]
        bits = [f"ref {os.path.basename(ref)}", f"send {r['send_offset_db']:+.2f} dB"]
        if r["ring_hz"]:
            bits.append(f"ring {r['ring_hz']:.0f} Hz")
        bits.append("sine20 H1..H5 " + " ".join(f"{v:.0f}" for v in h[:5]))
        cv = r["curve"]
        top = cv["out_db"][-1] - cv["bypass_db"][-1]
        bot = cv["out_db"][0] - cv["bypass_db"][0]
        bits.append(f"ramp gain {bot:+.1f} dB at -40, {top:+.1f} dB at 0 dBFS")
        sp = r["sweep_points"]
        if sp:
            bits.append("sweep " + " ".join(f"{p['env_dbfs']:.0f}:{p['f_hz']:.0f}Hz" for p in sp[-5:]))
        print(" | ".join(bits))
        print(r["png"])
        return

    if a.effect == "3BEQ":
        # a static EQ: the clicks/LFO/storage analyses have nothing to say
        # about it. Its measurement is the frequency response against the
        # grid's flat row, which is analysis/eq_response.py's job -- run that
        # and produce its CSV + PNG so the bench app has something to show.
        from analysis.eq_response import analyze_one, DEFAULT_BYPASS
        ref = a.ref_damp0 or a.ref_fb0 or a.eq_ref or _flat_row(a.captures_dir or os.path.dirname(os.path.abspath(a.capture))) or DEFAULT_BYPASS
        r = analyze_one(a.capture, ref, a.signal, a.layout, outdir=outdir)
        res = {"capture": a.capture, "effect": a.effect, "align": r["align"],
               "reference": ref, "eq": {k: v for k, v in r.items() if k != "align"}}
        with open(os.path.join(outdir, name + ".analysis.json"), "w") as f:
            json.dump(res, f, indent=1, default=float)
        p_ = r["fit"]
        bits = [f"align {r['align'].get('method')} offset {r['align'].get('offset_s', 0):.4f} s",
                f"ref {os.path.basename(ref)}", f"gain {p_['gain_db']:+.2f} dB"]
        for label, band in (("low", "low"), ("mid", "mid"), ("high", "high")):
            if p_[band + "_fc_hz"] is not None:
                bits.append(f"{label} {p_[band + '_fc_hz']:.0f} Hz Q{p_[band + '_q']:.2f} {p_[band + '_gain_db']:+.2f} dB")
        bits.append(f"resid {r['fit_residual_rms_db']:.3f} dB rms")
        bits.append(f"noise-sweep {r['cross_check']['noise_vs_sweep_rms_db']:.3f} dB rms"
                    + ("  ** LEVEL-DEPENDENT: the block is clipping **"
                       if r["cross_check"]["noise_vs_sweep_rms_db"] > 0.1 else ""))
        print(" | ".join(bits))
        print(r["png"] or r["csv"])
        return

    cap, ainfo = load_aligned(a.capture, x, fs, layout)
    res = {"capture": a.capture, "effect": a.effect, "align": ainfo}
    if a.effect == "BYPASS":
        from analysis.chain import analyze_chain
        res["chain"] = analyze_chain(cap, fs, layout, x)
        with open(os.path.join(outdir, name + ".analysis.json"), "w") as f:
            json.dump(res, f, indent=1, default=float)
        c = res["chain"]
        print(f"align {ainfo.get('method')} offset {ainfo.get('offset_s', 0):.4f} s | send {ainfo.get('ref_gain_db', 0):+.1f} dB | "
              f"latency {c['latency_ms']:.3f} ms | gain {c['gain_1k_db']:+.1f} dB | response 60 Hz {c['resp_db'][60]:+.1f} 10 k {c['resp_db'][10000]:+.1f} 18 k {c['resp_db'][18000]:+.1f} dB | "
              f"ramp: compression starts {c['ramp_onset_dbfs']} dBFS" + (f" (Peak-ish), max {c['ramp_max_gain_drop_db']:.1f} dB" if c['ramp_onset_dbfs'] is not None else ""))
        print("(no plot for BYPASS)")
        return
    cl = analyze_clicks(cap, fs, layout, x_signal=x)
    res["clicks"] = {k: v for k, v in cl.items() if k != "clicks"}
    res["clicks"]["per_click"] = [{k: v for k, v in c.items() if k not in ("g1", "g2", "g2_norm", "bands_hz")} for c in cl["clicks"]]
    st = lf = None
    if a.effect in ("SDLY", "XDLY", "HDLY"):
        if cl.get("delay_ms_median"):
            st = analyze_storage(cap, fs, layout, cl["delay_ms_median"])
            if not store_is_lti(st):
                cl = analyze_clicks(cap, fs, layout, x_signal=x, use_sweep_ir=False)
            res["storage"] = {k: v for k, v in st.items() if k not in ("noise_bands_hz", "noise_ratio_db", "rate_div_scores")}
        if a.ref_damp0:
            rcap, _ = load_aligned(a.ref_damp0, x, fs, layout)
            res["damp_differential"] = {k: v for k, v in damp_from_reference(cl, analyze_clicks(rcap, fs, layout, x_signal=x)).items()
                                        if k not in ("ratio_db", "bands_hz")}
    else:
        lf = analyze_lfo(cap, fs, layout)
        res["lfo"] = {k: v for k, v in lf.items() if k not in ("dev_t", "dev", "d_ms")}
        lp = loop_position(cl, lf["depth_ms"])
        res["loop_position_from_clicks"] = lp
        if a.ref_fb0 and lp.get("nominal_delay_ms"):
            rcap, _ = load_aligned(a.ref_fb0, x, fs, layout)
            lf0 = analyze_lfo(rcap, fs, layout)
            res["burst_pitch"] = burst_pitch(cap, fs, layout, lp["nominal_delay_ms"], lf0["depth_ms"], lf0["peak_slope"])
    png = os.path.join(outdir, name + ".png")
    plot_capture(name, cap, fs, layout, cl, st, lf, png)
    res["png"] = png
    with open(os.path.join(outdir, name + ".analysis.json"), "w") as f:
        json.dump(res, f, indent=1, default=float)
    # one-line summary for the app's log
    bits = [f"align {ainfo.get('method')} offset {ainfo.get('offset_s', 0):.4f} s"]
    if ainfo.get("dropouts_s"):
        bits.append(f"** DROPOUTS in the reference at {', '.join(str(t) for t in ainfo['dropouts_s'][:5])} s — RECAPTURE **")
    if "ref_gain_db" in ainfo:
        bits.append(f"send {ainfo['ref_gain_db']:+.1f} dB")
    if cl.get("delay_ms_median"):
        bits.append(f"delay {cl['delay_ms_median']:.2f} ms")
    if cl.get("fb_coef") is not None:
        bits.append(f"fb {cl['fb_coef']:.3f} (spectral, err {cl.get('loop_fit_err_db', 0):.1f} dB)")
    if cl.get("fb_coef_peaks") is not None:
        bits.append(f"fb {cl['fb_coef_peaks']:.3f} (peaks)")
    if cl.get("damp_hz"):
        bits.append(f"damp {cl['damp_hz']:.0f} Hz")
    if "damp_differential" in res and res["damp_differential"].get("damp_hz"):
        bits.append(f"damp(diff) {res['damp_differential']['damp_hz']:.0f} Hz")
    if st:
        bits.append(f"store /{st.get('rate_div')} {st.get('companding_verdict')}")
    if lf:
        bits.append(f"LFO {lf['lfo_hz']:.3f} Hz depth {lf['depth_ms']:.2f} ms {lf['shape']}")
    if "burst_pitch" in res:
        bits.append(f"loop {res['burst_pitch'].get('verdict')}")
    print(" | ".join(bits))
    print(png)


if __name__ == "__main__":
    main()
