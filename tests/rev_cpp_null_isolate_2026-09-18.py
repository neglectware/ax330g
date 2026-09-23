#!/usr/bin/env python3
"""One-off verification harness for docs/rev-cpp-null-check-2026-09-18.md.

Renders a REV (or SDLY, for the control) setting through the current C++
core (tools/render/ax30g-render, current tree, i.e. build 12) and through
the current Python engine (engine/effects.py via engine/render.py's
render_spec, current models/*.json), under an explicitly given rate /
resampler half_mult / input-signal-file combination, and reports the null
in dB. Does not write to dsp/, engine/, models/ or plugin-chain/ -- the
model's on-disk sample_rate/resampler keys are overridden IN MEMORY only
(spec_path is still passed so load_chain() can still find
models/ax30g-chain.json relative to it), never written back.

    .venv/bin/python tests/rev_cpp_null_isolate_2026-09-18.py

Prints one row per case defined in CASES below. Not meant to be reused
past this task -- a throwaway harness for a throwaway question.
"""
import os
import subprocess
import sys
import tempfile

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

from engine.render import load_spec, render_spec  # noqa: E402

RENDER_BIN = os.path.join(ROOT, "tools", "render", "ax30g-render")
REV_MODEL = os.path.join(ROOT, "models", "ax30g-rev.json")
SDLY_MODEL = os.path.join(ROOT, "models", "ax30g-sdly.json")

TYPE_IDX = {"ROOM": 0, "HALL": 1, "PLATE": 2}


def db(diff, ref):
    d = float(np.sqrt(np.mean(diff.astype(np.float64) ** 2)))
    r = float(np.sqrt(np.mean(ref.astype(np.float64) ** 2)))
    if d == 0.0:
        return float("-inf")
    return 20.0 * np.log10(max(d, 1e-300) / max(r, 1e-300))


def run_cpp_rev(signal_path, rate, mult, type_str, predly, revtime, highdamp, balance):
    chain = f"REV:{TYPE_IDX[type_str]},{predly},{int(round(revtime * 10))},{highdamp},{balance}"
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tf:
        out_path = tf.name
    cmd = [RENDER_BIN, signal_path, out_path, "--rate", repr(rate), "--chain", chain, "--chain-stage"]
    if mult is not None:
        cmd += ["--resampler-mult", str(mult)]
    subprocess.run(cmd, check=True, capture_output=True)
    y, fs = sf.read(out_path, dtype="float64", always_2d=True)
    os.unlink(out_path)
    return y, fs


def run_cpp_sdly(signal_path, rate, mult, ldly, rdly, lfb, rfb, damp, lbal, rbal):
    chain = f"SDLY:{ldly},{rdly},{lfb},{rfb},{damp},{lbal},{rbal}"
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tf:
        out_path = tf.name
    cmd = [RENDER_BIN, signal_path, out_path, "--rate", repr(rate), "--chain", chain, "--chain-stage"]
    if mult is not None:
        cmd += ["--resampler-mult", str(mult)]
    subprocess.run(cmd, check=True, capture_output=True)
    y, fs = sf.read(out_path, dtype="float64", always_2d=True)
    os.unlink(out_path)
    return y, fs


def run_py_rev(signal_path, rate, mult, type_str, predly, revtime, highdamp, balance):
    spec = load_spec(REV_MODEL)
    spec["sample_rate"] = rate
    if mult is None:
        spec.pop("resampler", None)  # fall back to resampler_window's own default (10)
    else:
        spec["resampler"] = {"half_mult": mult}
    spec["params"] = {"Type": type_str, "Pre Dly": predly, "Rev Time": revtime,
                       "High Damp": highdamp, "Balance": balance}
    x, fs = sf.read(signal_path, dtype="float64", always_2d=True)
    x = x[:, 0]
    y, _ = render_spec(spec, x, fs, spec_path=REV_MODEL)
    return y, fs


def run_py_sdly(signal_path, rate, mult, ldly, rdly, lfb, rfb, damp, lbal, rbal):
    spec = load_spec(SDLY_MODEL)
    spec["sample_rate"] = rate
    if mult is None:
        spec.pop("resampler", None)
    else:
        spec["resampler"] = {"half_mult": mult}
    spec["params"] = {"L Dly": ldly, "R Dly": rdly, "L Fb": lfb, "R Fb": rfb,
                       "High Damp": damp, "L Bal": lbal, "R Bal": rbal}
    x, fs = sf.read(signal_path, dtype="float64", always_2d=True)
    x = x[:, 0]
    y, _ = render_spec(spec, x, fs, spec_path=SDLY_MODEL)
    return y, fs


def null_rev(signal_path, rate, mult, type_str, predly, revtime, highdamp, balance):
    yc, _ = run_cpp_rev(signal_path, rate, mult, type_str, predly, revtime, highdamp, balance)
    yp, _ = run_py_rev(signal_path, rate, mult, type_str, predly, revtime, highdamp, balance)
    n = min(len(yc), len(yp))
    yc, yp = yc[:n], yp[:n]
    nulls = []
    for ch in range(2):
        nulls.append(db(yc[:, ch] - yp[:, ch], yp[:, ch]))
    return max(nulls)  # worse (higher, closer to 0) of the two channels


def null_sdly(signal_path, rate, mult, ldly, rdly, lfb, rfb, damp, lbal, rbal):
    yc, _ = run_cpp_sdly(signal_path, rate, mult, ldly, rdly, lfb, rfb, damp, lbal, rbal)
    yp, _ = run_py_sdly(signal_path, rate, mult, ldly, rdly, lfb, rfb, damp, lbal, rbal)
    n = min(len(yc), len(yp))
    yc, yp = yc[:n], yp[:n]
    nulls = []
    for ch in range(2):
        nulls.append(db(yc[:, ch] - yp[:, ch], yp[:, ch]))
    return max(nulls)


SIGNAL_NORMAL = os.path.join(ROOT, "capture", "signalset-normal.wav")
SIGNAL_TAIL = os.path.join(ROOT, "capture", "signalset-tail.wav")
RATE_B = 39063.829787
RATE_ALT = 39057.15

SIX = [
    ("HALL", 1, 2.0, 0, 25),
    ("ROOM", 1, 2.0, 0, 25),
    ("PLATE", 1, 2.0, 0, 25),
    ("HALL", 1, 10.0, 50, 50),
    ("HALL", 1, 0.1, 0, 50),
    ("HALL", 1, 2.0, 25, 25),
]


def main():
    print("## Part 1: six settings, build-11 conditions (rate 39063.829787, mult=10 matched both sides, signalset-normal.wav)\n")
    for t, pd, rt, hd, bal in SIX:
        n = null_rev(SIGNAL_NORMAL, RATE_B, 10, t, pd, rt, hd, bal)
        print(f"{t} RT{rt} HD{hd} Bal{bal}: {n:.2f} dB")

    print("\n## Part 2: six settings, build-12 conditions (rate 39063.829787, mult=100 matched both sides, signalset-normal.wav)\n")
    for t, pd, rt, hd, bal in SIX:
        n = null_rev(SIGNAL_NORMAL, RATE_B, 100, t, pd, rt, hd, bal)
        print(f"{t} RT{rt} HD{hd} Bal{bal}: {n:.2f} dB")

    print("\n## Part 3: 2x2x2 isolation on HALL RT2 HD0 Bal25\n")
    for rate, rate_lbl in [(RATE_B, "39063.829787"), (RATE_ALT, "39057.15")]:
        for mult in [10, 100]:
            for sig, sig_lbl in [(SIGNAL_NORMAL, "normal(56.7s)"), (SIGNAL_TAIL, "tail(125s)")]:
                n = null_rev(sig, rate, mult, "HALL", 1, 2.0, 0, 25)
                print(f"rate={rate_lbl} mult={mult} signal={sig_lbl}: {n:.2f} dB")

    print("\n## Part 4: SDLY control (300/300, Fb25, Bal25), both condition sets\n")
    for mult, lbl in [(10, "build-11 (mult=10)"), (100, "build-12 (mult=100)")]:
        n = null_sdly(SIGNAL_NORMAL, RATE_B, mult, 300, 300, 25, 25, 0, 25, 25)
        print(f"{lbl}, signalset-normal.wav: {n:.2f} dB")


if __name__ == "__main__":
    main()
