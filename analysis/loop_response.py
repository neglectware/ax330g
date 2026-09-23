"""Per-pass loop gain vs frequency from the decay of a high-feedback tail.

Method (drift-immune): split the tail into consecutive blocks of `block_s`
seconds, Welch-average the power spectrum inside each block, subtract the
capture's own noise floor (measured the same way on the silence segment),
and fit log(P_bin) against block time. The slope in dB/s divided by the
repeat rate 1/T gives the per-pass magnitude in dB at that frequency.

Blocks several repeat periods long make the measurement independent of where
each repeat lands, which matters for a MODULATED loop: the repeat times
wander by a few ms per pass, so windows tiled at a fixed period drift out of
step and fake a decay. Only the repeat rate itself enters, as a scale factor
on the dB/pass axis.
"""
import numpy as np
from scipy.signal import welch


def block_spectra(y, fs, t0, t1, block_s=1.0, nfft=4096):
    """(times, f, P[nblocks, nbins]) Welch power spectra of consecutive blocks."""
    nb = int(round(block_s * fs))
    i0, i1 = int(t0 * fs), int(t1 * fs)
    n = (i1 - i0) // nb
    P = []
    t = []
    for k in range(n):
        s = y[i0 + k * nb: i0 + (k + 1) * nb]
        f, p = welch(s, fs=fs, nperseg=nfft, noverlap=nfft // 2, window="hann",
                     detrend=False, scaling="density")
        P.append(p)
        t.append(t0 + (k + 0.5) * block_s)
    return np.array(t), f, np.array(P)


def per_pass(y, fs, t0, t1, period_s, floor_t=(0.1, 1.9), block_s=1.0, nfft=4096,
             snr_db=8.0, min_blocks=4):
    """Per-pass magnitude vs frequency. Returns (f, g, dbpass, nblocks, dbps)."""
    t, f, P = block_spectra(y, fs, t0, t1, block_s, nfft)
    _, _, Pf = block_spectra(y, fs, floor_t[0], floor_t[1], min(block_s, floor_t[1] - floor_t[0]), nfft)
    nf = np.median(Pf, axis=0)
    nbins = len(f)
    dbps = np.full(nbins, np.nan)
    nused = np.zeros(nbins, int)
    for k in range(nbins):
        v = P[:, k] - nf[k]
        ok = (P[:, k] > nf[k] * 10 ** (snr_db / 10.0)) & (v > 0)
        nused[k] = ok.sum()
        if ok.sum() < min_blocks:
            continue
        A = np.vstack([np.ones(ok.sum()), t[ok]]).T
        c, *_ = np.linalg.lstsq(A, np.log(v[ok]), rcond=None)
        dbps[k] = c[1] * 10.0 / np.log(10.0)     # dB/s of POWER -> dB/s of amplitude
    dbpass = dbps * period_s
    return f, 10 ** (dbpass / 20.0), dbpass, nused, dbps


def at(f, vals, hz, frac=1 / 6.):
    """Average `vals` over a 1/3-octave-ish band around hz (nan-safe)."""
    r = 2.0 ** frac
    m = (f >= hz / r) & (f <= hz * r) & np.isfinite(vals)
    return float(np.mean(vals[m])) if m.any() else float("nan")
