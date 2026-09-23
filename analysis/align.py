"""Align a capture to the signal set using the reference channel.

Capture layout: channel 0 = AX30G L, 1 = AX30G R, 2 = reference (the other
leg of the stereo pair that fed the pedal, looped straight back into the
interface). The reference is the test signal as it was actually played, so
cross-correlating it with the signal set gives the offset to sub-sample
precision and the send level in dBFS, whatever the pedal did to L/R.

Without a reference channel the sweep in the dry component is used instead;
that only works when the capture has dry signal in it (balance < max).
"""
import json
import numpy as np
from scipy.signal import correlate
from .util import seg, cut


def _peak_offset(template, signal, fs):
    c = correlate(signal, template, mode="full", method="fft")
    k = int(np.argmax(np.abs(c)))
    if 0 < k < len(c) - 1:
        a, b, d = abs(c[k - 1]), abs(c[k]), abs(c[k + 1])
        den = a - 2 * b + d
        frac = 0.5 * (a - d) / den if den != 0 else 0.0
    else:
        frac = 0.0
    lag = k - (len(template) - 1) + frac
    return lag, float(abs(c[k]))


def find_offset(chan, fs, x_signal, layout, use="sweep"):
    """Return (offset_seconds, score). offset is where the signal set's t=0
    sits in `chan`. Uses the sweep segment as the matched-filter template and
    cross-checks with the click train."""
    names = [q["name"] for q in layout["segments"]]
    if use not in names:            # e.g. the LFO signal set has no sweep: fall back to the long tone, then clicks
        use = "sine20" if "sine20" in names else "clicks"
    s = seg(layout, use)
    template = cut(x_signal, fs, s["start"], s["end"])
    lag, score = _peak_offset(template, chan, fs)
    offset = lag / fs - s["start"]
    return offset, score


def check_with_clicks(chan, fs, x_signal, layout, offset):
    """Cross-check the sweep offset with the clicks. The search is confined
    to +-0.1 s around the expected position: the clicks are quiet (-16 dBFS
    since 2026-09-13) and a whole-file correlation locks onto the ramp."""
    s = seg(layout, "clicks")
    template = cut(x_signal, fs, s["start"], s["end"])
    exp = (s["start"] + offset) * fs
    a = int(max(exp - 0.1 * fs, 0))
    b = int(min(exp + len(template) + 0.1 * fs, len(chan)))
    lag, _ = _peak_offset(template, chan[a:b], fs)
    return (a + lag) / fs - s["start"] - offset


def align_capture(cap, fs, x_signal, layout, ref_channel=2):
    """cap: (N, C). Returns (aligned (M,2) at signal-set timing, info dict).
    Samples before the offset are dropped; the result is trimmed/padded to
    the signal set's length plus 3 s of tail."""
    info = {}
    if cap.shape[1] > ref_channel:
        ref = cap[:, ref_channel]
        offset, score = find_offset(ref, fs, x_signal, layout)
        info["method"] = "reference"
        # send level: RMS of the reference over the -20 dBFS sine vs the signal set's
        s = seg(layout, "sine20")
        r = cut(ref, fs, offset + s["start"] + 0.5, offset + s["end"] - 0.5)
        x = cut(x_signal, fs, s["start"] + 0.5, s["end"] - 0.5)
        info["ref_gain_db"] = float(20 * np.log10((np.sqrt(np.mean(r ** 2)) + 1e-12) / (np.sqrt(np.mean(x ** 2)) + 1e-12)))
        info["ref_peak_dbfs"] = float(20 * np.log10(np.max(np.abs(ref)) + 1e-12))
    else:
        offset, score = find_offset(cap[:, 0], fs, x_signal, layout)
        info["method"] = "dry-sweep"
    info["offset_s"] = float(offset)
    info["click_disagreement_ms"] = float(check_with_clicks(cap[:, 0] if cap.shape[1] <= ref_channel else cap[:, ref_channel],
                                                            fs, x_signal, layout, offset) * 1000)
    n0 = int(round(offset * fs))
    length = len(x_signal) + int(3 * fs)
    nch = min(cap.shape[1], 3)   # L, R and (if present) the reference, all aligned
    out = np.zeros((length, nch))
    src = cap[max(n0, 0):max(n0, 0) + length, :nch]
    dst0 = max(-n0, 0)
    out[dst0:dst0 + len(src)] = src
    # sub-sample residual is well under the analysis' 0.02 ms resolution; recorded, not applied
    info["subsample_residual_ms"] = float((offset * fs - n0) / fs * 1000)
    # dropout check: digital silence in the reference while the signal set is playing
    if cap.shape[1] > ref_channel:
        r = out[:, ref_channel] if out.shape[1] > ref_channel else None
        if r is not None:
            q = int(0.25 * fs)
            drops = [round(i / fs, 1) for i in range(int(2.5 * fs), min(len(r), len(x_signal)) - q, q)
                     if np.abs(r[i:i + q]).max() == 0.0 and np.abs(x_signal[i:i + q]).max() > 1e-4]
            info["dropouts_s"] = drops
    return out, info
