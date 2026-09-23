"""SUPERSEDED 2026-09-14 by models/ax30g-chain.json + out/null/chain-loop.npy
(see docs/chain-fit-2026-09-14.md); kept for history.

Derive the linear 'chain EQ' the model lacks: the capture's dry impulse
response divided by the model's own dry impulse response (both from the
sweep), so converter roll-off already present in the model's resampling is
not counted twice. Uses a Feedback 0 capture. Writes an FIR (.npy) for
null_test.py --eq.

    chain_eq.py captures/AX30G_SDLY_..._LFb-0_RFb-0_..._IN-LIN.wav [--model M] [--out out/null/chain-eq.npy]
"""
import argparse
import os
import sys
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
from analysis.util import load_layout            # noqa: E402
from analysis.run import load_aligned            # noqa: E402
from analysis.ir import sweep_ir                 # noqa: E402
from engine.render import load_spec, render_spec  # noqa: E402
from tests.null_test import params_from_name     # noqa: E402


def dry_ir(sig, fs, layout, x, pre_ms=3.0, post_ms=120.0):
    ir, i0 = sweep_ir(sig, fs, layout, x_signal=x, channel=0, length_s=0.3)
    pk = int(np.argmax(np.abs(ir[i0 - int(0.002 * fs):i0 + int(0.005 * fs)]))) + i0 - int(0.002 * fs)
    pre, post = int(pre_ms * fs / 1000), int(post_ms * fs / 1000)
    h = ir[pk - pre:pk + post].copy()
    e = int(0.002 * fs)
    ramp = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, e))
    h[:e] *= ramp
    h[-e:] *= ramp[::-1]
    return h, pre


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture")
    ap.add_argument("--model", default=os.path.join(ROOT, "models", "ax30g-sdly.json"))
    ap.add_argument("--signal", default=os.path.join(ROOT, "capture", "signalset.wav"))
    ap.add_argument("--layout", default=os.path.join(ROOT, "capture", "layout.json"))
    ap.add_argument("--out", default=os.path.join(ROOT, "out", "null", "chain-eq.npy"))
    ap.add_argument("--f-hi", type=float, default=19000.0)
    ap.add_argument("--passthrough", action="store_true", help="model = converters only (for a BYPASS capture)")
    a = ap.parse_args()
    x, fs = sf.read(a.signal, dtype="float64", always_2d=True)
    x = x[:, 0]
    layout = load_layout(a.layout)
    name = os.path.splitext(os.path.basename(a.capture))[0]
    cap, _ = load_aligned(a.capture, x, fs, layout)
    spec = load_spec(a.model)
    spec["params"].update(params_from_name(name))
    if a.passthrough:
        spec["params"].update({"L Fb": 0, "R Fb": 0, "High Damp": 0, "L Bal": 0, "R Bal": 0})
    y, _ = render_spec(spec, x, fs)
    y3 = np.concatenate([y[: cap.shape[0]], cap[: y.shape[0], 2:3]], axis=1)   # model L/R + the real reference
    hc, pre = dry_ir(cap, fs, layout, x)
    hm, _ = dry_ir(y3, fs, layout, x)
    n = 1 << 14
    Hc, Hm = np.fft.rfft(hc, n), np.fft.rfft(hm, n)
    f = np.fft.rfftfreq(n, 1.0 / fs)
    eps = 1e-3 * np.abs(Hm).max() ** 2
    Hq = Hc * np.conj(Hm) / (np.abs(Hm) ** 2 + eps)
    W = np.ones_like(f)
    W[f > a.f_hi] = 0.0
    e = (f >= a.f_hi * 0.9) & (f <= a.f_hi)
    W[e] = 0.5 + 0.5 * np.cos(np.pi * (f[e] - a.f_hi * 0.9) / (a.f_hi * 0.1))
    q = np.fft.irfft(Hq * W, n)
    # the quotient is near-symmetric around 0; take -3 ms .. +120 ms (the
    # coupling high-pass has a long tail) and window; the first repeat is at
    # >= 195 ms in every grid, outside the window
    L = int(0.123 * fs)
    h = np.concatenate([q[-int(0.003 * fs):], q[: L - int(0.003 * fs)]])
    e2 = int(0.002 * fs)
    e3 = int(0.040 * fs)
    ramp = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, e2))
    ramp3 = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, e3))
    h[:e2] *= ramp
    h[-e3:] *= ramp3[::-1]
    H = np.fft.rfft(h, 8192)
    fr = np.fft.rfftfreq(8192, 1.0 / fs)
    g1k = abs(H[np.argmin(np.abs(fr - 1000))])
    h /= g1k
    H /= g1k
    print("chain EQ (capture dry / model dry), dB re 1 kHz:")
    print("  " + " ".join(f"{f0}:{20 * np.log10(abs(H[np.argmin(np.abs(fr - f0))])):+.2f}" for f0 in [10, 15, 20, 30, 40, 60, 100, 150, 200, 300, 500, 1000, 2000, 5000, 10000, 15000, 18000, 19000]))
    print("phase (deg): " + " ".join(f"{f0}:{np.degrees(np.angle(H[np.argmin(np.abs(fr - f0))] * np.exp(2j * np.pi * fr[np.argmin(np.abs(fr - f0))] * (int(0.003 * fs)) / fs))):+.0f}" for f0 in [10, 15, 20, 30, 40, 60, 100, 200, 500, 1000, 5000, 10000, 15000, 18000, 19000]))
    np.save(a.out, h)
    print("wrote", a.out, "taps", len(h))


if __name__ == "__main__":
    main()
