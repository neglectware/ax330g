"""Residual WAVs for listening: capture, model (old and new, one rms-matched gain each, fixed 14.87-sample
lag as tests/hypr_null.py) and capture-minus-new-model, 48 kHz float, into out/hypr-2/."""
import os, sys, json
import numpy as np
import soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from q import *
from pyguard import quiet
ROOT = "/path/to/ax30g"
sys.path.insert(0, ROOT)
from engine.render import load_spec, render_spec
import engine.hypr  # noqa
from tests.null_test import frac_shift
from tests.hypr_null import params_from_name
from meas import X, FS, load, LAY, seg

OUT = os.path.join(ROOT, "out", "hypr-2")
os.makedirs(OUT, exist_ok=True)


def render(model, name):
    sp = os.path.join(ROOT, "models", model)
    spec = json.loads(json.dumps(load_spec(sp)))
    spec["params"].update(params_from_name(name))
    if "IN-MAX" in name:
        st = dict(spec.get("input_stage") or {}); st["input_level_db"] = 14.0509; spec["input_stage"] = st
    y, _ = render_spec(spec, X, FS, spec_path=sp)
    return frac_shift(y[:, 0], 14.87)


def gfit(c, m):
    mask = np.ones(len(c), bool)
    rp = seg(LAY, "ramp"); mask[int(rp["start"] * FS):int(rp["end"] * FS)] = False
    # level match (rms over everything but the ramp), not a correlation fit: on a fuzz the
    # waveforms barely correlate and a least-squares gain comes out 10-30 dB low
    return float(np.sqrt(np.mean(c[mask] ** 2) / max(np.mean(m[mask] ** 2), 1e-30)))


if __name__ == "__main__":
    picks = sys.argv[1:]
    for n in picks:
        quiet()
        c = load(n)[:len(X)]
        mo = render("ax30g-hypr.json", n)[:len(c)]
        quiet()
        mn = render("ax30g-hypr-2.json", n)[:len(c)]
        go, gn = gfit(c, mo), gfit(c, mn)
        tag = n.replace("AX30G_HYPR_", "")
        sf.write(os.path.join(OUT, tag + "__capture.wav"), c.astype(np.float32), FS, subtype="FLOAT")
        sf.write(os.path.join(OUT, tag + "__model-0918.wav"), (go * mo).astype(np.float32), FS, subtype="FLOAT")
        sf.write(os.path.join(OUT, tag + "__model-0923.wav"), (gn * mn).astype(np.float32), FS, subtype="FLOAT")
        sf.write(os.path.join(OUT, tag + "__residual-0923.wav"), (c - gn * mn).astype(np.float32), FS, subtype="FLOAT")
        print("wrote", tag, "gains %.2f / %.2f dB" % (20 * np.log10(abs(go) + 1e-12), 20 * np.log10(abs(gn) + 1e-12)), flush=True)
