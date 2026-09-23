"""Modulation-delay analysis: LFO rate, depth and shape from a pitch track of
the wet 1 kHz sine, plus the in-loop vs after-loop question from click timing.

Pitch track: instantaneous frequency of the analytic signal. For a delay
d(t) the wet tone's frequency is f0 * (1 - d'(t)), so the frequency deviation
IS the LFO's derivative: a triangle LFO gives a square-wave deviation, a sine
LFO a sinusoidal one, and a stepped (control-rate) LFO gives spikes.
"""
import numpy as np
from scipy.signal import hilbert, butter, sosfiltfilt
from .util import cut, seg


def pitch_track(x, fs, f0=1000.0, bw=700.0, smooth_ms=0.3):
    sos = butter(4, [f0 - bw, f0 + bw], btype="band", fs=fs, output="sos")
    y = sosfiltfilt(sos, x)
    a = hilbert(y)
    ph = np.unwrap(np.angle(a))
    f = np.diff(ph) * fs / (2 * np.pi)
    n = max(1, int(smooth_ms * fs / 1000))
    if n > 1:
        f = np.convolve(f, np.ones(n) / n, mode="same")
    return f


def analyze_lfo(x, fs, layout, f0=1000.0, channel=0):
    ch = x[:, channel]
    s = seg(layout, "sine20")
    w = cut(ch, fs, s["start"] + 0.4, s["end"] - 0.05)
    f = pitch_track(w, fs, f0)
    trim = int(0.05 * fs)                     # filter edge transients
    f = f[trim:-trim]
    dev = 1.0 - f / f0                        # = d'(t), dimensionless
    dev = dev - np.mean(dev)
    t = np.arange(len(dev)) / fs
    # LFO rate from the deviation spectrum, zero-padded + parabolic interpolation
    n = len(dev)
    nfft = 1 << (int(np.ceil(np.log2(n))) + 3)
    D = np.abs(np.fft.rfft(dev * np.hanning(n), nfft))
    fr = np.fft.rfftfreq(nfft, 1.0 / fs)
    m = np.where((fr >= 0.05) & (fr <= 25.0))[0]
    k = m[np.argmax(D[m])]
    if 0 < k < len(D) - 1:
        a, b, c = D[k - 1], D[k], D[k + 1]
        delta = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
    else:
        delta = 0.0
    lfo_hz = float((k + delta) * fs / nfft)
    # delay modulation waveform: integrate d'(t)
    d = np.cumsum(dev) / fs
    d = d - np.mean(d)
    # remove residual linear drift
    p = np.polyfit(t, d, 1)
    d = d - np.polyval(p, t)
    depth_ms = float((d.max() - d.min()) / 2.0 * 1000.0)
    # shape: RMS/peak of the deviation, computed on a lowpassed copy so
    # stepped-LFO spikes do not decide it; spikes are measured separately.
    sos = butter(4, 150.0, btype="low", fs=fs, output="sos")
    dev_lp = sosfiltfilt(sos, dev)
    # crest on the lowpassed deviation, ignoring the 1 % extreme samples (corner ringing)
    pk = np.quantile(np.abs(dev_lp), 0.99)
    crest = float(np.sqrt(np.mean(dev_lp ** 2)) / (pk + 1e-12))
    # square (triangle LFO) -> ~1.0 ; sine -> ~0.71
    shape = "tri" if crest > 0.85 else "sin"
    peak_slope = float(np.quantile(np.abs(dev_lp), 0.995))
    hf = dev - dev_lp
    step_ratio = float(np.sqrt(np.mean(hf ** 2)) / (np.sqrt(np.mean(dev_lp ** 2)) + 1e-12))
    return {
        "lfo_hz": lfo_hz, "depth_ms": depth_ms, "shape": shape, "crest": crest, "peak_slope": peak_slope,
        "step_ratio": step_ratio, "stepped": step_ratio > 0.3,
        "dev_t": t[::48].tolist(), "dev": dev_lp[::48].tolist(), "d_ms": (d[::48] * 1000).tolist(),
    }


def loop_position(clicks_res, depth_ms):
    """In-loop vs after-loop from repeat arrival offsets.

    After-loop: every repeat of one click is read through the same modulated
    pointer at (nearly) the same LFO phase-offset, so arrival_j - j*D stays
    within +-depth. In-loop: the modulation applied on each pass accumulates,
    so the offset of repeat j can reach j*depth.
    """
    good = [c for c in clicks_res["clicks"] if "delay_ms" in c]
    if not good:
        return {"error": "no clicks"}
    # nominal delay: mean of all first-repeat delays and spacings (LFO averages out)
    allv = [c["delay_ms"] for c in good] + [v for c in good for v in c.get("spacings_ms", [])]
    D = float(np.mean(allv))
    rows = []
    from .clicks import split_dry
    for c in good:
        _, reps = split_dry(c["peaks"])
        t_rel = [p[0] for p in reps]
        offs = [t_rel[j] - (j + 1) * D for j in range(len(t_rel))]
        rows.append(offs)
    maxj = max(len(r) for r in rows)
    by_j = [[r[j] for r in rows if len(r) > j] for j in range(maxj)]
    spread = [float(np.max(np.abs(v))) for v in by_j]
    ratio = spread[min(2, len(spread) - 1)] / (spread[0] + 1e-9) if spread else None
    verdict = None
    if len(spread) >= 3 and depth_ms:
        # after-loop keeps every repeat inside +-depth (plus timing noise)
        verdict = "in_loop" if spread[2] > 1.6 * depth_ms else "after_loop"
    return {"nominal_delay_ms": D, "offsets_ms": rows, "max_abs_offset_by_repeat_ms": spread,
            "compounding_ratio": ratio, "verdict": verdict}


def burst_pitch(x, fs, layout, delay_ms, depth_ms, peak_slope, f0=1000.0, channel=0, n_rep=4, min_rel_db=-30.0):
    """Per-repeat pitch deviation of short sine bursts.

    After-loop: every repeat is read through the one modulated pointer, so
    |deviation| of repeat j never exceeds the LFO's peak slope (for a triangle
    LFO it is the same for every j). In-loop: each pass adds the modulation
    again, so repeat j can carry up to j times the slope. peak_slope comes from
    the Feedback 0 capture of the same Speed/Depth (analyze_lfo()["peak_slope"]).
    """
    ch = x[:, channel]
    s = seg(layout, "bursts")
    blen = s["len_s"]
    D = delay_ms / 1000.0
    dep = depth_ms / 1000.0
    rows = []
    ref_amp = None
    for tb in s["burst_times"]:
        devs = []
        for j in range(1, n_rep + 1):
            t0 = tb + j * D - dep - 0.005
            t1 = tb + j * D + dep + blen + 0.005
            w = cut(ch, fs, t0 - 0.02, t1 + 0.02)
            f = pitch_track(w, fs, f0, bw=700.0, smooth_ms=0.3)
            from scipy.signal import butter, sosfiltfilt, hilbert
            sos = butter(4, [f0 - 700, f0 + 700], btype="band", fs=fs, output="sos")
            env = np.abs(hilbert(sosfiltfilt(sos, w)))
            pk = env.max()
            if ref_amp is None:
                ref_amp = pk
            if pk < ref_amp * 10 ** (min_rel_db / 20):
                break
            on = np.where(env > 0.5 * pk)[0]
            if len(on) < int(0.01 * fs):
                break
            a, b = on[0], on[-1]
            c0 = a + int(0.3 * (b - a))
            c1 = a + int(0.7 * (b - a))
            devs.append(float(np.mean(1.0 - f[c0:c1] / f0)))
        rows.append(devs)
    by_j = [[r[j] for r in rows if len(r) > j] for j in range(n_rep)]
    peak_by_j = [float(np.max(np.abs(v))) if v else None for v in by_j]
    valid = [p for p in peak_by_j if p is not None]
    ratio = None
    verdict = None
    if len(valid) >= 3 and peak_slope > 0:
        ratio = max(valid[1:]) / peak_slope
        verdict = "in_loop" if ratio > 1.4 else "after_loop"
    return {"dev_by_burst": rows, "peak_abs_dev_by_repeat": peak_by_j,
            "compounding_ratio": ratio, "verdict": verdict}
