#!/usr/bin/env python3
"""Per-repeat decay of a high-feedback delay tail.

Tiles a segment's tail into windows one repeat-period long, measures the
energy in each window (broadband and per band, noise-floor corrected from the
silence segment), and fits the per-pass loop gain from the slope of
log-energy vs repeat number. Also reports the per-pass spectrum
(repeat n / repeat n-1) and the DC offset and noise floor of the loop.

    tail_decay.py CAPTURE.wav [--segment clicks] [--period-ms 249.6458]

Captures are read from out/tail/<name>.aligned.npy if present (see the
caching step in docs/smod-feedback-2026-09-16.md), else aligned on the fly.
"""
import argparse
import json
import os
import sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
from analysis.util import load_layout, seg   # noqa: E402

FS = 48000.0
BANDS = [100.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 6000.0, 8000.0, 10000.0,
         12000.0, 14000.0, 16000.0, 18000.0]


def db(v):
    return 20.0 * np.log10(np.maximum(np.asarray(v, float), 1e-30))


def load_cap(path):
    name = os.path.splitext(os.path.basename(path))[0]
    cached = os.path.join(ROOT, "out", "tail", name + ".aligned.npy")
    if os.path.exists(cached):
        return np.load(cached), FS, name
    import soundfile as sf
    from analysis.run import load_aligned
    x, fs = sf.read(os.path.join(ROOT, "capture", "signalset-tail.wav"), dtype="float64", always_2d=True)
    layout = load_layout(os.path.join(ROOT, "capture", "layout-tail.json"))
    cap, _ = load_aligned(path, x[:, 0], fs, layout)
    os.makedirs(os.path.dirname(cached), exist_ok=True)
    np.save(cached, cap)
    return cap, fs, name


def band_edges(fc, frac=1/3.):
    r = 2.0 ** (frac / 2.0)
    return fc / r, fc * r


def window_spectra(y, fs, i0, nper, nwin):
    """Energy per window, windows CENTRED on i0 + n*nper (so an impulsive
    repeat sits at the peak of the Hann taper, not on its edge -- with the
    event 20 ms into a 250 ms Hann window it is attenuated 24 dB and every
    band ratio is wrong). Returns (E_total[nwin], E_band[nwin, nbands],
    spectra, f). E is the plain rectangular mean square of the window; the
    band energies are Hann-windowed and power-compensated, one-sided."""
    E = np.zeros(nwin)
    B = np.zeros((nwin, len(BANDS)))
    w = np.hanning(nper)
    wnorm = np.mean(w ** 2)
    f = np.fft.rfftfreq(nper, 1.0 / fs)
    idx = [np.where((f >= band_edges(fc)[0]) & (f < band_edges(fc)[1]))[0] for fc in BANDS]
    S = np.zeros((nwin, len(f)))
    half = nper // 2
    for n in range(nwin):
        a = i0 + n * nper - half
        s = y[max(a, 0):max(a, 0) + nper]
        if len(s) < nper:
            s = np.pad(s, (0, nper - len(s)))
        E[n] = np.mean(s ** 2)
        P = 2.0 * np.abs(np.fft.rfft(s * w)) ** 2 / (nper * nper * wnorm)
        P[0] *= 0.5
        if nper % 2 == 0:
            P[-1] *= 0.5
        S[n] = P
        for k, ii in enumerate(idx):
            B[n, k] = P[ii].sum()
    return E, B, S, f


def fit_gain(vals, floor, n0, n1, min_snr_db=40.0):
    """Fit g from E_n = A g^(2n) over n in [n0, n1) where E_n - floor is at
    least min_snr_db above the floor. Returns (g, n_used, rms_resid_db)."""
    n = np.arange(len(vals))
    v = vals - floor
    ok = (n >= n0) & (n < n1) & (vals > floor * 10 ** (min_snr_db / 10.0)) & (v > 0)
    if ok.sum() < 5:
        return float("nan"), int(ok.sum()), float("nan")
    ln = np.log(v[ok])
    A = np.vstack([np.ones(ok.sum()), n[ok]]).T
    coef, *_ = np.linalg.lstsq(A, ln, rcond=None)
    resid = ln - A @ coef
    g = float(np.exp(coef[1] / 2.0))
    return g, int(ok.sum()), float(np.std(resid) * 10.0 / np.log(10) / 2.0)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture")
    ap.add_argument("--segment", default="clicks")
    ap.add_argument("--period-ms", type=float, default=249.642854,
                     help="loop period. The default is the SMOD Dly-250 period measured "
                          "2026-09-16 (249.642854 ms, spread 4 ns); set it for any other delay time.")
    ap.add_argument("--snr-gate", type=float, default=40.0,
                     help="only fit repeats whose energy is this far above the noise floor (dB). "
                          "Below ~30 dB the fitted gain drifts upward; see the module docstring.")
    ap.add_argument("--layout", default=os.path.join(ROOT, "capture", "layout-tail.json"))
    ap.add_argument("--skip", type=int, default=1, help="repeats to skip at the start of the fit")
    ap.add_argument("--nmax", type=int, default=200)
    ap.add_argument("--json", help="write the numbers here")
    a = ap.parse_args()

    cap, fs, name = load_cap(a.capture)
    layout = load_layout(a.layout)
    s = seg(layout, a.segment)
    sil = seg(layout, "silence")
    nper = int(round(a.period_ms * fs / 1000.0))
    out = {"capture": name, "segment": a.segment, "period_ms": a.period_ms, "period_samples": nper,
           "snr_gate_db": a.snr_gate, "channels": []}
    print(f"{name}\n  segment {a.segment} {s['start']}-{s['end']} s, period {a.period_ms:.6f} ms = {nper} samples, "
          f"SNR gate {a.snr_gate:.0f} dB")
    for ch, side in enumerate("LR"):
        y = cap[:, ch]
        # noise floor from the silence segment
        i_s = int(sil["start"] * fs) + 1000
        j_s = int(sil["end"] * fs) - 1000
        nf = y[i_s:j_s]
        nwin_f = max(1, len(nf) // nper)
        Ef, Bf, _, _ = window_spectra(nf, fs, 0, nper, nwin_f)
        floor_E = float(np.median(Ef))
        floor_B = np.median(Bf, axis=0)
        # the event: first sample above 1/3 of the segment peak
        i_ev = int(s["start"] * fs)
        j_ev = int(min(s["end"], len(y) / fs) * fs)
        w = y[i_ev:j_ev]
        pk = np.max(np.abs(w[: int(0.5 * fs)]))
        i_on = i_ev + int(np.argmax(np.abs(w[: int(0.5 * fs)]) > 0.33 * pk))
        # window n is centred on repeat n+1
        i0 = i_on + nper
        nwin = min(a.nmax, (j_ev - i0 - nper // 2) // nper)
        E, B, S, f = window_spectra(y, fs, i0, nper, nwin)
        g, nu, rsd = fit_gain(E, floor_E, a.skip, nwin, a.snr_gate)
        gb = []
        for k, fc in enumerate(BANDS):
            gk, nk, rk = fit_gain(B[:, k], floor_B[k], a.skip, nwin, a.snr_gate)
            gb.append((fc, gk, nk, rk))
        dc = [float(np.mean(y[i0 + n * nper - nper // 2: i0 + n * nper + nper // 2])) for n in range(nwin)]
        print(f" {side}: broadband g = {g:.4f}  ({nu} repeats used, fit resid {rsd:.2f} dB/repeat)")
        print(f"    repeat 1 rms {db(np.sqrt(E[0])):.1f} dBFS, floor {db(np.sqrt(floor_E)):.1f} dBFS, "
              f"loop DC max |{max(abs(v) for v in dc):.2e}|")
        print("    band     g       dB/pass   reps")
        for fc, gk, nk, rk in gb:
            print(f"    {fc:7.0f}  {gk:7.4f}  {20*np.log10(gk) if gk==gk else float('nan'):+7.3f}  {nk:4d}")
        out["channels"].append({
            "side": side, "g_broadband": g, "n_used": nu, "fit_resid_db": rsd,
            "floor_dbfs": float(db(np.sqrt(floor_E))),
            "repeat_rms_dbfs": [float(v) for v in db(np.sqrt(np.maximum(E - floor_E, 1e-30)))],
            "bands": [{"hz": fc, "g": gk, "db_per_pass": float(20 * np.log10(gk)) if gk == gk else None,
                       "n": nk, "resid_db": rk} for fc, gk, nk, rk in gb],
            "dc_per_window": dc,
        })
    if a.json:
        with open(a.json, "w") as fh:
            json.dump(out, fh, indent=1, default=float)
        print("wrote", a.json)


if __name__ == "__main__":
    main()
