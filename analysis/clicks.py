"""Click-train analysis for the delay family: repeat times, amplitudes,
feedback coefficient and the loop filter (High Damp) fitted as a one-pole.
"""
import math
import numpy as np
from scipy.signal import find_peaks
from .util import cut, band_spectrum, db
from .ir import sweep_ir, ir_window


def find_repeats(x, fs, t_click, max_ms=1200.0, n_max=6, thresh=0.02):
    """Return list of (t_rel_ms, amp) for the dry click (t_rel=0) and repeats."""
    w = cut(x, fs, t_click - 0.005, t_click + max_ms / 1000.0)
    env = np.abs(w)
    peak = env.max()
    dist = int(0.003 * fs)
    idx, props = find_peaks(env, height=thresh * peak, distance=dist)
    if len(idx) == 0:
        return []
    t0 = int(0.005 * fs)
    # The real unit's click carries a slow tail (~20 dB down, decaying over
    # ~20 ms; input coupling) whose ripples pass the height test. A repeat is
    # an abrupt event: require it to stand 12 dB above the envelope in the
    # 1-5 ms before it, and to come at least 4 ms after the dry click (the
    # manual's minimum delay is 5 ms). The dry click itself is exempt.
    out = []
    dry_i = None
    for i in idx:
        t_ms = (i - t0) * 1000.0 / fs
        if abs(t_ms) < 1.0 and dry_i is None:
            dry_i = i
            out.append((t_ms, float(env[i])))
            continue
        if dry_i is not None and (i - dry_i) < int(0.004 * fs):
            continue
        pre = env[max(i - int(0.005 * fs), 0):max(i - int(0.001 * fs), 1)]
        if pre.size and env[i] < 4.0 * pre.max():
            continue
        out.append((t_ms, float(env[i])))
    return out[:n_max + 1]


def store_is_lti(st):
    """True when the storage verdict allows sweep-derived spectra (linear,
    full-rate). Companded or decimated stores are level-dependent / aliasing
    and the click windows are the safer source there."""
    if not st:
        return True
    return (st.get("rate_div") in (None, 1)) and (st.get("companding_verdict") in (None, "linear PCM"))


def split_dry(peaks):
    """(dry_peak_or_None, repeats) with times relative to the click instant."""
    dry = None
    reps = []
    for t, a in peaks:
        if abs(t) < 1.0 and dry is None:
            dry = (t, a)
        elif t > 1.0:
            reps.append((t, a))
    return dry, reps


def repeat_window(x, fs, t_abs_s, pre_ms=1.0, post_ms=30.0):
    w = cut(x, fs, t_abs_s - pre_ms / 1000.0, t_abs_s + post_ms / 1000.0)
    n = len(w)
    # tukey-ish edges so the FFT does not see the cut
    e = int(0.001 * fs)
    ramp = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, e))
    w = w.copy()
    w[:e] *= ramp
    w[-e:] *= ramp[::-1]
    return w


DEVICE_FS = 39062.5


def onepole_mag(f, fc, fs_dev=DEVICE_FS):
    """Magnitude of the engine's OnePoleLP (y += a (x - y), a = 1 - exp(-2 pi fc / fs))
    evaluated at the device rate, so the fit is in the same coordinates the
    model uses and stays honest near the device's Nyquist (a one-pole at
    39 kHz flattens toward -9 dB, unlike the analytic RC curve)."""
    f = np.asarray(f, dtype=float)
    if fc is None:
        return np.ones_like(f)
    pole = math.exp(-2.0 * math.pi * fc / fs_dev)
    w = 2.0 * np.pi * f / fs_dev
    return (1.0 - pole) / np.sqrt(1.0 - 2.0 * pole * np.cos(w) + pole * pole)


def normalize_shape(fc_bands, g, f_lo=150.0, f_hi=600.0):
    m = (fc_bands >= f_lo) & (fc_bands <= f_hi) & np.isfinite(g)
    return g / np.nanmean(g[m])


def fit_loop(fc_bands, ratio, f_lo=150.0, f_hi=9000.0, mask=None):
    """Fit ratio(f) ~ g * onepole(f, fc). Returns (g, fc_or_None, rms_err_db).
    mask: bands to trust (e.g. where the storage response is not in a null)."""
    m = (fc_bands >= f_lo) & (fc_bands <= f_hi) & np.isfinite(ratio) & (ratio > 0)
    if mask is not None:
        m &= mask
    f = fc_bands[m]
    r = db(ratio[m])
    best = None
    cands = [None] + list(np.geomspace(40.0, 19000.0, 160))
    for fc in cands:
        h = db(onepole_mag(f, fc))
        g_db = np.mean(r - h)
        err = np.sqrt(np.mean((r - h - g_db) ** 2))
        if best is None or err < best[2] - 1e-9:
            best = (10 ** (g_db / 20.0), fc, err)
    return best


def analyze_clicks(x, fs, layout, channel=0, max_ms=1200.0, x_signal=None, use_sweep_ir=True):
    """x: (N,C) capture. Returns dict with per-click repeat tables and fits.
    Repeat *timing* and peak amplitudes come from the clicks; repeat *spectra*
    come from the sweep-derived impulse response when use_sweep_ir (the real
    unit's -16 dBFS clicks leave later repeats under the noise floor)."""
    s = None
    ir = None
    if use_sweep_ir:
        try:
            ir, ir0 = sweep_ir(x, fs, layout, x_signal=x_signal, channel=channel)
            # the IR's dry impulse marks t=0 for the repeat windows
            pk = int(np.argmax(np.abs(ir[ir0 - int(0.002 * fs):ir0 + int(0.005 * fs)]))) + ir0 - int(0.002 * fs)
        except Exception as e:   # no sweep segment, or no reference and no signal
            ir = None
    for seg in layout["segments"]:
        if seg["name"] == "clicks":
            s = seg
    ch = x[:, channel]
    res = {"clicks": [], "channel": channel}
    all_ratios = []
    first_delay = first_spacing = None
    first_fb = 0.0
    for k, tc in enumerate(s["click_times"]):
        peaks = find_repeats(ch, fs, tc, max_ms=min(max_ms, 1900.0))
        if first_delay and k > 0 and first_fb > 0:
            # At high feedback an earlier click's repeats are still audible
            # under this click (Fb 40: 1.9 dB per repeat). Drop a peak that
            # sits on an earlier click's repeat grid (within 2.5 ms) *while
            # that repeat would still clear the height threshold*; at low
            # feedback the grid is silent long before the next click, and a
            # genuine coincidence (the synthetic set's 4th click) survives.
            n_audible = math.log(0.02) / math.log(first_fb) if first_fb < 1 else 1e9
            keep = []
            for t, a in peaks:
                t_abs = tc + t / 1000.0
                ghost = False
                for tj in s["click_times"][:k]:
                    n = (t_abs - tj) * 1000.0 / first_spacing
                    if 1 <= n <= n_audible and abs(n - round(n)) * first_spacing < 2.5:
                        ghost = True
                        break
                if not ghost or abs(t) < 1.0:
                    keep.append((t, a))
            peaks = keep
        dry, reps = split_dry(peaks)
        if k == 0 and reps:
            first_delay = reps[0][0]
            first_spacing = reps[1][0] - reps[0][0] if len(reps) >= 2 else first_delay
            first_fb = (reps[1][1] / reps[0][1]) if len(reps) >= 2 and reps[0][1] > 0 else 0.0
        entry = {"t_click": tc, "peaks": peaks, "has_dry": dry is not None}
        if len(reps) >= 1:
            t_rel = [r[0] for r in reps]
            # delay is dry-to-first-repeat; the dry itself lands a few tenths
            # of a ms after the reference click (device latency)
            entry["delay_ms"] = t_rel[0] - (dry[0] if dry is not None else 0.0)
            entry["latency_ms"] = dry[0] if dry is not None else None
            entry["spacings_ms"] = [t_rel[i + 1] - t_rel[i] for i in range(len(t_rel) - 1)]
            entry["amp_ratios"] = [reps[i + 1][1] / reps[i][1] for i in range(len(reps) - 1)]
            if ir is not None:
                t_dry = dry[0] if dry is not None else 0.0
                wins = [ir_window(ir, pk, fs, r[0] - t_dry) for r in reps[:3]]
            else:
                wins = [repeat_window(ch, fs, tc + r[0] / 1000.0) for r in reps[:3]]
            fcb, _ = band_spectrum(wins[0], fs)
            spectra = [band_spectrum(w, fs)[1] for w in wins]
            entry["bands_hz"] = fcb.tolist()
            shape = None
            mask = None
            if dry is not None:
                dwin = ir_window(ir, pk, fs, 0.0) if ir is not None else repeat_window(ch, fs, tc + dry[0] / 1000.0)
                dspec = band_spectrum(dwin, fs)[1]
                g1 = spectra[0] / dspec
                entry["g1"] = g1.tolist()
                shape = normalize_shape(fcb, g1)
                mask = shape > 0.25   # ignore bands the storage already kills (>12 dB down)
            if len(spectra) >= 2:
                r21 = spectra[1] / spectra[0]
                entry["g2"] = r21.tolist()
                g, fc, err = fit_loop(fcb, r21)
                entry["loop_fit_raw"] = {"fb_coef": g, "damp_hz": fc, "err_db": err}
                if shape is not None:
                    r21n = r21 / shape
                    entry["g2_norm"] = r21n.tolist()
                    g, fc, err = fit_loop(fcb, r21n, mask=mask)
                entry["loop_fit"] = {"fb_coef": g, "damp_hz": fc, "err_db": err,
                                     "storage_normalized": shape is not None}
                all_ratios.append(r21n if shape is not None else r21)
                if len(spectra) >= 3:
                    r32 = spectra[2] / spectra[1]
                    if shape is not None:
                        r32 = r32 / shape
                    g3, fc3, err3 = fit_loop(fcb, r32, mask=mask)
                    entry["loop_fit_32"] = {"fb_coef": g3, "damp_hz": fc3, "err_db": err3}
        res["clicks"].append(entry)
    good = [c for c in res["clicks"] if "delay_ms" in c]
    if good:
        res["delay_ms_median"] = float(np.median([c["delay_ms"] for c in good]))
        sp = [v for c in good for v in c.get("spacings_ms", [])]
        res["spacing_ms_all"] = sp
        ar = [v for c in good for v in c.get("amp_ratios", [])[:1]]   # first-to-second repeat, peak amplitude
        if ar:
            res["fb_coef_peaks"] = float(np.median(ar))
        res["spectra_source"] = "sweep_ir" if ir is not None else "clicks"
        fits = [c["loop_fit"] for c in good if "loop_fit" in c]
        if fits:
            res["fb_coef"] = float(np.median([f["fb_coef"] for f in fits]))
            fcs = [f["damp_hz"] for f in fits if f["damp_hz"] is not None]
            res["damp_hz"] = float(np.median(fcs)) if len(fcs) >= max(1, len(fits) // 2 + 1) else None
            res["loop_fit_err_db"] = float(np.median([f["err_db"] for f in fits]))
        if all_ratios:
            res["g2_mean"] = np.nanmean(np.array(all_ratios), axis=0).tolist()
        g1s = [c["g1"] for c in good if "g1" in c]
        if all_ratios or g1s:
            res["bands_hz"] = good[0]["bands_hz"]
        if g1s:
            res["g1_mean"] = np.nanmean(np.array(g1s), axis=0).tolist()
            res["shape_mask"] = (normalize_shape(np.array(res["bands_hz"]), np.array(res["g1_mean"])) > 0.25).tolist()
    return res
