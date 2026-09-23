"""M1: delay-line storage format. Uses the wet-only tail after the sine and
noise segments end (the last `delay` ms is one repeat with nothing under it).
Reports SINAD at two levels (linear PCM drops ~20 dB for a 20 dB level drop,
companding much less) and the repeat/dry spectrum ratio (bandwidth, aliasing).
"""
import numpy as np
from .util import cut, seg, band_spectrum, welch_bands, db


def sinad(x, fs, f0=1000.0, span_bins=3):
    n = len(x)
    w = np.hanning(n)
    X = np.abs(np.fft.rfft(x * w)) ** 2
    f = np.fft.rfftfreq(n, 1.0 / fs)
    k = int(np.argmin(np.abs(f - f0)))
    k = k - span_bins + int(np.argmax(X[k - span_bins:k + span_bins + 1]))
    sig = X[max(0, k - span_bins):k + span_bins + 1].sum()
    band = (f >= 30.0) & (f <= fs / 2 - 200)
    band[max(0, k - span_bins):k + span_bins + 1] = False
    band[: int(np.argmin(np.abs(f - 30.0)))] = False
    rest = X[band].sum()
    return 10.0 * np.log10(sig / max(rest, 1e-30)), 10.0 * np.log10(np.mean(x ** 2) + 1e-30)


def floor_level(x, fs, f0=1000.0):
    """Mean noise PSD (dB) in bands between the harmonics of f0: 1.2-1.8, 2.2-2.8, 3.2-3.8 kHz."""
    n = len(x)
    X = np.abs(np.fft.rfft(x * np.hanning(n))) ** 2 / n
    f = np.fft.rfftfreq(n, 1.0 / fs)
    m = np.zeros_like(f, dtype=bool)
    for k in (1, 2, 3):
        m |= (f >= k * f0 + 200) & (f <= (k + 1) * f0 - 200)
    return 10.0 * np.log10(np.mean(X[m]) + 1e-30)


def image_peaks(x, fs, f0=1000.0, rel_floor_db=-75.0, guard_hz=25.0):
    """Spectral peaks in the wet-only sine tail that are not harmonics of f0.
    A stored-at-lower-rate delay line puts images of f0 at k*R +- f0."""
    n = len(x)
    X = np.abs(np.fft.rfft(x * np.hanning(n)))
    f = np.fft.rfftfreq(n, 1.0 / fs)
    k0 = int(np.argmin(np.abs(f - f0)))
    ref = X[k0 - 3:k0 + 4].max()
    Xdb = 20 * np.log10(X / ref + 1e-12)
    band = (f > 200) & (f < fs / 2 - 200)
    floor = float(np.median(Xdb[band]))
    from scipy.signal import find_peaks
    idx, _ = find_peaks(Xdb, height=max(rel_floor_db, floor + 20.0), distance=int(guard_hz / (fs / n)), prominence=12.0)
    out = []
    for i in idx:
        fi = f[i]
        if fi < 200 or fi > fs / 2 - 200:
            continue
        if abs(fi / f0 - round(fi / f0)) * f0 < guard_hz:
            continue  # harmonic of f0 (companding/waveshaping distortion, not an image)
        out.append((float(fi), float(Xdb[i])))
    out.sort(key=lambda p: -p[1])
    return out[:12]


def rate_div_from_images(peaks, fs_dev, f0=1000.0, tol_hz=25.0):
    """Score rate dividers 2..8 by how well k*fs_dev/n +- f0 explains the peaks."""
    if not peaks:
        return 1, {}
    scores = {}
    for n in range(2, 9):
        R = fs_dev / n
        pred = []
        for k in range(1, n + 1):
            for sgn in (-1, 1):
                fp = k * R + sgn * f0
                if f0 + 100 < fp < fs_dev / 2 - 100:
                    pred.append(fp)
        sc = 0.0
        hits = 0
        for fi, lv in peaks:
            if any(abs(fi - fp) < tol_hz for fp in pred):
                sc += lv + 90.0
                hits += 1
        scores[n] = {"score": sc, "hits": hits, "predicted": pred}
    best = max(scores, key=lambda n: (scores[n]["score"], -n))
    if scores[best]["hits"] == 0:
        return 1, scores
    # prefer the smallest n whose prediction set is not a strict superset of nothing:
    # /4 predicts everything /2 predicts, so require the extra images to be present
    for n in sorted(scores):
        if scores[n]["hits"] >= 1 and scores[n]["score"] >= 0.95 * scores[best]["score"]:
            return n, scores
    return best, scores


def wet_tail(x, fs, s, delay_ms, guard_ms=12.0):
    t0 = s["end"] + guard_ms / 1000.0
    t1 = s["end"] + delay_ms / 1000.0 - 4.0 / 1000.0
    if t1 - t0 < 0.02:
        return None
    return cut(x, fs, t0, t1)


def dry_head(x, fs, s, delay_ms, guard_ms=20.0):
    t0 = s["start"] + guard_ms / 1000.0
    t1 = s["start"] + delay_ms / 1000.0 - 4.0 / 1000.0
    if t1 - t0 < 0.02:
        return None
    return cut(x, fs, t0, t1)


def analyze_storage(x, fs, layout, delay_ms, channel=0, fs_dev=39062.5):
    ch = x[:, channel]
    res = {"delay_ms_used": delay_ms}
    for name in ("sine20", "sine40"):
        s = seg(layout, name)
        tail = wet_tail(ch, fs, s, delay_ms)
        head = dry_head(ch, fs, s, delay_ms)
        if tail is None or head is None:
            res[name] = {"error": "delay too short for a wet-only tail"}
            continue
        sn_w, lvl_w = sinad(tail, fs)
        sn_d, lvl_d = sinad(head, fs)
        res[name] = {"wet_sinad_db": sn_w, "wet_level_db": lvl_w,
                     "dry_sinad_db": sn_d, "dry_level_db": lvl_d,
                     "wet_floor_db": floor_level(tail, fs), "dry_floor_db": floor_level(head, fs)}
        if name == "sine20":
            res["image_peaks"] = image_peaks(tail, fs)
            res["rate_div"], res["rate_div_scores"] = rate_div_from_images(res["image_peaks"], fs_dev)
    if "wet_floor_db" in res.get("sine20", {}) and "wet_floor_db" in res.get("sine40", {}):
        res["sinad_drop_db"] = res["sine20"]["wet_sinad_db"] - res["sine40"]["wet_sinad_db"]
        # the between-harmonics noise floor: linear PCM keeps it where it is when the
        # signal drops 20 dB; a companded store's noise follows the signal down.
        res["floor_drop_db"] = res["sine20"]["wet_floor_db"] - res["sine40"]["wet_floor_db"]
        res["companding_verdict"] = ("companded" if res["floor_drop_db"] > 6.0 else "linear PCM")
    s = seg(layout, "noise")
    tail = wet_tail(ch, fs, s, delay_ms)
    head = dry_head(ch, fs, s, delay_ms)
    if tail is not None and head is not None:
        nfft = 2048
        fc, mw = welch_bands(tail, fs, nfft=nfft)
        _, md = welch_bands(head, fs, nfft=nfft)
        ratio = mw / md
        ref = np.nanmean(ratio[(fc >= 200) & (fc <= 1000)])
        rel = db(ratio / ref)
        res["noise_bands_hz"] = fc.tolist()
        res["noise_ratio_db"] = rel.tolist()

        def corner(level):
            m = np.where((fc > 1000) & (rel <= level))[0]
            return float(fc[m[0]]) if len(m) else None
        res["bw_3db_hz"] = corner(-3.0)
        res["bw_10db_hz"] = corner(-10.0)
        res["bw_20db_hz"] = corner(-20.0)
    return res
