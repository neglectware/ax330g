"""Null test: render the signal set through a model spec with the capture's
own parameters, align it to the real capture, fit one gain, and report how
far the residual sits below the capture, per segment. Writes
out/null/<capture>.residual.wav (ch0 capture L, ch1 model L, ch2 residual L).

    null_test.py captures/AX30G_SDLY_....wav [--model models/ax30g-sdly.json]
                 [--eq out/null/chain-eq.npy]   # optional dry-chain FIR
"""
import argparse
import json
import os
import re
import sys
import numpy as np
import soundfile as sf
from scipy.signal import fftconvolve

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
from analysis.util import load_layout, seg, cut   # noqa: E402
from analysis.run import load_aligned              # noqa: E402
from engine.render import load_spec, render_spec, load_chain, apply_chain, load_input_stage   # noqa: E402

PARAM_RE = re.compile(r"(LDly|RDly|LFb|RFb|HighDamp|LBal|RBal|DlyTime|Feedback|Speed|Depth"
                      r"|Bass|MidFreq|MidGain|Treble|Trim|Ducking)-(-?\d+(?:\.\d+)?)")
NAMES = {"LDly": "L Dly", "RDly": "R Dly", "LFb": "L Fb", "RFb": "R Fb", "HighDamp": "High Damp",
         "LBal": "L Bal", "RBal": "R Bal", "DlyTime": "Dly Time", "Feedback": "Feedback",
         "Speed": "Speed", "Depth": "Depth",
         # 3BEQ (the unit displays the last one as "Trim Gain"; the file name says Trim)
         "Bass": "Bass", "MidFreq": "Mid Freq", "MidGain": "Mid Gain",
         "Treble": "Treble", "Trim": "Trim Gain",
         # the four Ambience delays' Ducking (docs/ducking-model-2026-09-18.md)
         "Ducking": "Ducking"}


def params_from_name(name):
    out = {}
    for k, v in PARAM_RE.findall(name):
        out[NAMES[k]] = float(v) if "." in v else int(v)
    return out


def db(v):
    return 20 * np.log10(max(float(v), 1e-12))


def best_shift(a, b, fs, max_ms=5.0):
    """Lag (samples, sub-sample) that best aligns b to a, by cross-correlation."""
    n = int(max_ms * fs / 1000)
    c = fftconvolve(a, b[::-1], mode="full")
    mid = len(b) - 1
    w = c[mid - n:mid + n + 1]
    k = int(np.argmax(w))
    if 0 < k < len(w) - 1:
        y0, y1, y2 = w[k - 1], w[k], w[k + 1]
        den = y0 - 2 * y1 + y2
        frac = 0.5 * (y0 - y2) / den if den != 0 else 0.0
    else:
        frac = 0.0
    return (k - n) + frac


def refine_lag(c, m, lag0, mask, span=0.3, iters=18):
    """Refine a lag by minimising the residual energy of c - g*shift(m)
    (g refit each time) over lag0 +- span samples, golden section. The
    parabolic peak of the cross-correlation is biased by a few hundredths of
    a sample for band-limited signals; at 10-18 kHz that alone costs 10-20 dB
    of null (found 2026-09-14 -- see docs/chain-fit-2026-09-14.md)."""
    def cost(lag):
        m2 = frac_shift(m, lag)
        g = float(np.dot(c[mask], m2[mask]) / max(np.dot(m2[mask], m2[mask]), 1e-20))
        r = c[mask] - g * m2[mask]
        return float(np.dot(r, r))
    a, b = lag0 - span, lag0 + span
    gr = (np.sqrt(5.0) - 1.0) / 2.0
    c1, c2 = b - gr * (b - a), a + gr * (b - a)
    f1, f2 = cost(c1), cost(c2)
    for _ in range(iters):
        if f1 < f2:
            b, c2, f2 = c2, c1, f1
            c1 = b - gr * (b - a)
            f1 = cost(c1)
        else:
            a, c1, f1 = c1, c2, f2
            c2 = a + gr * (b - a)
            f2 = cost(c2)
    return 0.5 * (a + b)


def frac_shift(x, lag):
    """Shift x by +lag samples (fractional) with an FFT phase ramp."""
    n = len(x)
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(n)
    return np.fft.irfft(X * np.exp(-2j * np.pi * f * lag), n)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture")
    ap.add_argument("--model", default=os.path.join(ROOT, "models", "ax30g-sdly.json"))
    ap.add_argument("--signal", default=os.path.join(ROOT, "capture", "signalset.wav"))
    ap.add_argument("--layout", default=os.path.join(ROOT, "capture", "layout.json"))
    ap.add_argument("--eq", help="npy FIR applied to the model output. With a model that carries its own "
                                 "'chain' (models/ax30g-chain.json) this should be the MEASUREMENT chain only, "
                                 "out/null/chain-loop.npy (the Scarlett loopback, not part of the unit); "
                                 "the pre-2026-09-14 chain-eq-bypass.npy folded both together.")
    ap.add_argument("--no-refine", action="store_true",
                     help="keep the parabolic cross-correlation lag instead of refining it on the residual "
                          "(reproduces pre-2026-09-14 numbers)")
    ap.add_argument("--model-wav", help="use this pre-rendered model output (e.g. from ax30g-render) instead of the Python engine")
    ap.add_argument("--out", default=os.path.join(ROOT, "out", "null"))
    ap.add_argument("--lfo-phase", type=float, default=None,
                     help="MODD only: override blocks.lfo.phase (0-1). The real unit's LFO free-runs from "
                          "power-on, so its phase at the moment a capture starts is not a device law and "
                          "must be fit per capture (see docs/modd-model-2026-09-13.md).")
    ap.add_argument("--lfo-rate", type=float, default=None,
                     help="MODD only: override the Speed param's Hz value with a capture's own precisely "
                          "measured LFO rate (from its .analysis.json) instead of the nominal panel value, "
                          "so phase does not drift across the ~57 s render. Nulling aid only, not a model law "
                          "(see docs/modd-model-2026-09-13.md).")
    ap.add_argument("--input-stage", nargs="?", const="ax30g-input-stage.json",
                     help="switch the analog input stage on for this render (default file: "
                          "models/ax30g-input-stage.json). Off unless given; no model spec carries the key.")
    ap.add_argument("--input-level-db", type=float, default=None,
                     help="Input Level in dB relative to LIN for --input-stage (MAX = +14.0509).")
    ap.add_argument("--clip-mode", choices=["hard_clip", "none"], default=None,
                     help="override input_stage.ceiling.type (none = shelf and de-emphasis only, "
                          "for the 'plain hard clip with the shelf' baseline comparisons)")
    ap.add_argument("--headroom-dbfs", type=float, default=None,
                     help="override input_stage.ceiling.headroom_dbfs, the input file's 0 dBFS below the "
                          "ADC ceiling at LIN (for --no-shelf, 1.477 keeps the 1 kHz onset where the shelf "
                          "model puts it)")
    ap.add_argument("--offset-frac", type=float, default=None,
                     help="override input_stage.ceiling.offset_frac (DC offset at the clipper, as a "
                          "fraction of the threshold; positive clips the positive half first)")
    ap.add_argument("--no-shelf", action="store_true",
                     help="drop the pre-emphasis/de-emphasis pair from --input-stage: a plain hard clip "
                          "at the same threshold, which is the baseline the shelf has to beat")
    ap.add_argument("--align-segment", default="sweep",
                     help="segment used to fit the single global lag (default: sweep, right for a static "
                          "SDLY-style delay). For a modulated effect the sweep sits under a moving delay too, "
                          "so a MODD null benefits from aligning on 'clicks' instead, close to where the LFO "
                          "phase was itself fit.")
    a = ap.parse_args()

    x, fs = sf.read(a.signal, dtype="float64", always_2d=True)
    x = x[:, 0]
    layout = load_layout(a.layout)
    name = os.path.splitext(os.path.basename(a.capture))[0]
    cap, info = load_aligned(a.capture, x, fs, layout)

    spec = load_spec(a.model)
    spec_path = os.path.abspath(a.model)
    spec["params"].update(params_from_name(name))
    if a.lfo_phase is not None:
        spec.setdefault("blocks", {}).setdefault("lfo", {})["phase"] = a.lfo_phase
    if a.lfo_rate is not None:
        spec["params"]["Speed"] = a.lfo_rate
    if a.input_stage:
        st = load_input_stage({"input_stage": a.input_stage}, spec_path)
        if a.input_level_db is not None:
            st["input_level_db"] = a.input_level_db
        if a.clip_mode:
            st.setdefault("ceiling", {})["type"] = a.clip_mode
        if a.headroom_dbfs is not None:
            st.setdefault("ceiling", {})["headroom_dbfs"] = a.headroom_dbfs
        if a.offset_frac is not None:
            st.setdefault("ceiling", {})["offset_frac"] = a.offset_frac
        if a.no_shelf:
            st["pre_emphasis"] = {}
        spec["input_stage"] = st
    if a.model_wav:
        y, fy = sf.read(a.model_wav, dtype="float64", always_2d=True)
        assert fy == fs, "model wav must be at the signal set's rate"
        # the C++ core renders blocks only; give it the same converter chain
        # the Python engine applies (its resampler is still the 10x design
        # until dsp/resampler.h grows the multiplier -- see docs/chain-fit-2026-09-14.md)
        y = apply_chain(y, fs, load_chain(spec, spec_path))
    else:
        y, truth = render_spec(spec, x, fs, spec_path=spec_path)
    if a.eq:
        h = np.load(a.eq)
        y = np.stack([fftconvolve(y[:, c], h, mode="full")[: y.shape[0]] for c in range(2)], axis=1)

    os.makedirs(a.out, exist_ok=True)
    res = {"capture": a.capture, "model": a.model, "params": spec["params"], "channels": []}
    segs = [s for s in layout["segments"] if s["name"] != "silence"]
    resid_out = None
    for ch, side in enumerate("LR"):
        c = cap[: len(y), ch]
        m = y[: len(c), ch]
        # align on the sweep (dense, broadband), then one gain over everything but the ramp
        sw = seg(layout, a.align_segment)
        lag0 = best_shift(cut(c, fs, sw["start"], sw["end"]), cut(m, fs, sw["start"], sw["end"]), fs)
        mask = np.ones(len(c), bool)
        try:
            rp = seg(layout, "ramp")
            mask[int(rp["start"] * fs):int(rp["end"] * fs)] = False
        except KeyError:
            pass   # the lfo layout variant (capture/layout-lfo.json) has no ramp segment
        lag = lag0 if a.no_refine else refine_lag(c, m, lag0, mask)
        m2 = frac_shift(m, lag)
        g = float(np.dot(c[mask], m2[mask]) / max(np.dot(m2[mask], m2[mask]), 1e-20))
        r = c - g * m2
        rows = []
        for s in segs:
            cs, rs = cut(c, fs, s["start"], s["end"]), cut(r, fs, s["start"], s["end"])
            rows.append((s["name"], db(np.sqrt(np.mean(cs ** 2))), db(np.sqrt(np.mean(rs ** 2)))))
        tot_c = db(np.sqrt(np.mean(c[mask] ** 2)))
        tot_r = db(np.sqrt(np.mean(r[mask] ** 2)))
        res["channels"].append({"side": side, "lag_samples": lag, "lag_parabolic": lag0, "gain_db": db(abs(g)),
                                "segments": [{"name": n_, "capture_dbfs": cd, "residual_dbfs": rd, "null_db": rd - cd} for n_, cd, rd in rows],
                                "null_db_total": tot_r - tot_c})
        if ch == 0:
            resid_out = np.stack([c, g * m2, r], axis=1)
        print(f"{side}: lag {lag:+.3f} samples (parabolic {lag0:+.3f}), gain {db(abs(g)):+.2f} dB, null {tot_r - tot_c:+.1f} dB overall")
        for n_, cd, rd in rows:
            print(f"   {n_:8s} capture {cd:6.1f}  residual {rd:6.1f}  null {rd - cd:+6.1f} dB")
    sf.write(os.path.join(a.out, name + ".residual.wav"), resid_out.astype(np.float32), int(fs), subtype="FLOAT")
    with open(os.path.join(a.out, name + ".null.json"), "w") as f:
        json.dump(res, f, indent=1, default=float)
    print("wrote", os.path.join(a.out, name + ".residual.wav"))


if __name__ == "__main__":
    main()
