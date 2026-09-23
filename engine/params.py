"""Parameter maps: user-facing values (0-50, ms, Hz) -> physical quantities.

The map types here are hypotheses about how the AX30G converts a panel value
to a coefficient. The analysis recovers the physical quantity; the capture grid
then tells us which map is right. Keep device facts in JSON, not here.
"""
import math


def apply_map(m, value, fs):
    t = m["type"]
    if t == "ms_to_samples":
        q = m.get("quantize_ms", 0)
        ms = round(value / q) * q if q else value
        return round(ms * fs / 1000.0)
    if t == "linear":
        in_min = m.get("in_min", 0.0)
        in_max = m.get("in_max", 50.0)
        out_min = m.get("out_min", 0.0)
        out_max = m["out_max"]
        f = (value - in_min) / (in_max - in_min)
        return out_min + f * (out_max - out_min)
    if t == "identity":
        return value
    if t == "constant":
        # a device fact with no panel parameter behind it (the AX30G Chorus'
        # fixed 939-sample delay and its fixed dry/wet mix)
        return m["value"]
    if t == "log_hz":
        # 0 -> None (no filter) unless hz_at_0 given; then log interpolation
        in_max = m.get("in_max", 50.0)
        hz0 = m.get("hz_at_0")
        hz1 = m["hz_at_max"]
        if value <= 0 and hz0 is None:
            return None
        if hz0 is None:
            hz0 = 19000.0
        f = value / in_max
        return math.exp(math.log(hz0) + f * (math.log(hz1) - math.log(hz0)))
    if t == "table":
        tab = m["table"]
        return tab[int(value)]
    if t == "samples_per_ms":
        # AX30G: the firmware uses an integer number of samples per displayed ms.
        # offset_samples (default 0, backwards compatible): a constant integer
        # added after rounding -- e.g. the 2026-09-16 delay-length scan's
        # "round(ms*39)+2" law (docs/sdly-clock-2026-09-16.md).
        return int(round(value * m["k"])) + int(m.get("offset_samples", 0))
    if t == "pole_linear":
        # first-order lowpass whose pole coefficient is k * value (AX30G High
        # Damp: pole ~ 0.0195 * n). Returned as the cutoff in Hz that
        # OnePoleLP turns back into exactly that pole: pole = exp(-2 pi fc / fs).
        pole = m["k"] * value
        if pole <= 0.0:
            return None
        pole = min(pole, m.get("max_pole", 0.9999))
        return -math.log(pole) * fs / (2.0 * math.pi)
    if t == "interp":
        # piecewise-linear through measured points [[value, out], ...]
        pts = m["points"]
        if value <= pts[0][0]:
            return pts[0][1]
        for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
            if x0 <= value <= x1:
                return y0 + (y1 - y0) * (value - x0) / (x1 - x0)
        return pts[-1][1]
    raise ValueError(f"unknown map type {t}")
