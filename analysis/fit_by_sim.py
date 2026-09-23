"""Fit a parameter by simulation: when the store aliases or compands, an
analytic one-pole no longer describes what the loop does to a repeat, but the
engine with the identified storage does. Render candidates, keep the closest.
"""
import copy
import json
import numpy as np
from engine import render_spec
from .clicks import analyze_clicks
from .differential import damp_from_reference
from .storage import wet_tail, image_peaks, seg as _seg


def clicks_only(x, fs, layout):
    return upto_segment(x, fs, layout, "clicks")


def upto_segment(x, fs, layout, name):
    s = _seg(layout, name)
    n = int((s["end"] + 1.0) * fs)
    lay = {"fs": layout["fs"], "segments": [g for g in layout["segments"] if g["end"] <= s["end"] + 1e-6]}
    return x[:n], lay


def predicted_images(rate_div, fs_dev, f0=1000.0):
    R = fs_dev / rate_div
    out = []
    for k in range(1, rate_div + 1):
        for sgn in (-1, 1):
            fp = k * R + sgn * f0
            if f0 + 100 < fp < fs_dev / 2 - 100:
                out.append(fp)
    return out


def pick_storage_by_images(template, storages, delay_ms, st_measured, x, fs, layout, f0=1000.0, tol_hz=25.0):
    """Choose among candidate storages by simulation: each is rendered at
    Feedback 0 and compared with the measured capture on (a) the levels of the
    sine's image tones at the frequencies the rate divider predicts (hold vs
    linear read) and (b) the noise repeat/dry spectrum ratio (prefilter: a
    plain decimation aliases noise flat, an averaging prefilter rolls it off).
    st_measured is analyze_storage() of the real Feedback 0 capture."""
    from .storage import analyze_storage
    xs, lay = upto_segment(x, fs, layout, "noise")
    spec = copy.deepcopy(template)
    spec["maps"]["high_damp"] = {"type": "identity"}
    spec["maps"]["feedback"] = {"type": "identity"}
    spec["maps"]["delay"] = {"type": "ms_to_samples", "quantize_ms": 0}
    for side in ("L", "R"):
        spec["params"][f"{side} Fb"] = 0.0
        spec["params"][f"{side} Dly"] = delay_ms
    spec["params"]["High Damp"] = None
    fs_dev = float(template.get("sample_rate", 39062.5))
    meas_peaks = st_measured.get("image_peaks", [])
    meas_noise = np.array(st_measured.get("noise_ratio_db", []))

    def level(peaks, fq):
        c = [l for f, l in peaks if abs(f - fq) <= tol_hz]
        return max(c) if c else -90.0

    ranked = []
    for st in storages:
        spec["blocks"]["delay_line"]["storage"] = st
        y, _ = render_spec(spec, xs, fs)
        sim = analyze_storage(y, fs, lay, delay_ms, fs_dev=fs_dev)
        pred = predicted_images(st["rate_div"], fs_dev, f0) if st["rate_div"] > 1 else []
        e_img = float(np.sqrt(np.mean([(level(meas_peaks, fq) - level(sim["image_peaks"], fq)) ** 2 for fq in pred]))) if pred else 0.0
        sn = np.array(sim.get("noise_ratio_db", []))
        e_noise = float(np.sqrt(np.nanmean((sn - meas_noise) ** 2))) if len(sn) == len(meas_noise) and len(sn) else 0.0
        ranked.append({"storage": st, "err_db": e_img + e_noise, "err_images_db": e_img, "err_noise_db": e_noise,
                       "sim_peaks": [(round(f), round(l, 1)) for f, l in sim["image_peaks"][:4]]})
    ranked.sort(key=lambda r: r["err_db"])
    return ranked


def storage_from_verdict(st_res, bits_linear=16, bits_companded=8):
    """Candidate storage dicts consistent with the M1 verdicts. Below full rate
    the prefilter/interpolation are not known from M1 alone, so all four
    combinations are candidates and the simulation fit decides."""
    comp = "mu" if st_res.get("companding_verdict") == "companded" else "none"
    bits = bits_companded if comp == "mu" else bits_linear
    rd = int(st_res.get("rate_div", 1))
    if rd == 1:
        return [{"bits": bits, "compand": comp, "rate_div": 1}]
    return [{"bits": bits, "compand": comp, "rate_div": rd, "prefilter": pf, "upsample": up}
            for pf in ("none", "avg") for up in ("hold", "linear")]


def fit_damp_by_sim(template, storages, fb_coef, delay_ms, measured_ratio_db, bands_hz, mask,
                    x, fs, layout, candidates=None, f_lo=150.0, f_hi=9000.0, verbose=False, use_sweep_ir=True, ratio_key=None):
    """Try every candidate storage; return the best (lowest error) fit, with the others listed."""
    if isinstance(storages, dict):
        storages = [storages]
    results = []
    for st in storages:
        r = _fit_one(template, st, fb_coef, delay_ms, measured_ratio_db, bands_hz, mask,
                     x, fs, layout, candidates, f_lo, f_hi, verbose, use_sweep_ir, ratio_key)
        r["storage"] = st
        results.append(r)
    results.sort(key=lambda r: r["err_db"])
    best = dict(results[0])
    best["alternatives"] = [{"storage": r["storage"], "damp_hz": r["damp_hz"], "err_db": r["err_db"]} for r in results[1:]]
    return best


def _fit_one(template, storage, fb_coef, delay_ms, measured_ratio_db, bands_hz, mask,
             x, fs, layout, candidates, f_lo, f_hi, verbose, use_sweep_ir=True, ratio_key=None):
    xc, lay = clicks_only(x, fs, layout)
    spec = copy.deepcopy(template)
    spec["blocks"]["delay_line"]["storage"] = storage
    spec["maps"]["high_damp"] = {"type": "identity"}
    spec["maps"]["feedback"] = {"type": "identity"}
    spec["maps"]["delay"] = {"type": "ms_to_samples", "quantize_ms": 0}
    for side in ("L", "R"):
        spec["params"][f"{side} Fb"] = fb_coef
        spec["params"][f"{side} Dly"] = delay_ms
    spec["params"]["High Damp"] = None
    y0, _ = render_spec(spec, xc, fs)
    cl0 = analyze_clicks(y0, fs, lay, x_signal=xc, use_sweep_ir=use_sweep_ir)
    f = np.array(bands_hz)
    m = (f >= f_lo) & (f <= f_hi) & np.array(mask) & np.isfinite(measured_ratio_db)
    if candidates is None:
        candidates = np.geomspace(800.0, 16000.0, 14)
    errs = []
    for fc in candidates:
        spec["params"]["High Damp"] = float(fc)
        y, _ = render_spec(spec, xc, fs)
        cl = analyze_clicks(y, fs, lay, x_signal=xc, use_sweep_ir=use_sweep_ir)
        dd = damp_from_reference(cl, cl0, key=ratio_key)
        r = np.array(dd["ratio_db"])
        e = float(np.sqrt(np.mean((r[m] - np.array(measured_ratio_db)[m]) ** 2)))
        errs.append(e)
        if verbose:
            print(f"  fc {fc:8.1f}  err {e:.3f} dB")
    errs = np.array(errs)
    k = int(np.argmin(errs))
    fc = float(candidates[k])
    if 0 < k < len(candidates) - 1:
        # parabolic refine in log-frequency
        lx = np.log(candidates[k - 1:k + 2])
        a, b, c = errs[k - 1:k + 2]
        den = a - 2 * b + c
        if den > 0:
            d = 0.5 * (a - c) / den
            fc = float(np.exp(lx[1] + d * (lx[2] - lx[1])))
    return {"damp_hz": fc, "err_db": float(errs[k]), "candidates": [float(c) for c in candidates],
            "errs_db": errs.tolist()}
