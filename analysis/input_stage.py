"""Measurement of the analog input stage (Input Level -> +15 dB amplifier with
pre-emphasis -> ADC full scale -> de-emphasis), from the Bypass LIN/MAX pair.

Everything here is relative: the LIN capture is the linear reference and the
MAX capture (Input Level fully clockwise, +14.05 dB) is the same signal set
driven into the converter's ceiling. The functions below are the measurements
written up in docs/input-stage-2026-09-16.md:

  gain_vs_level()   the MAX ramp's fundamental compression per 0.1 s, against
                    the LIN ramp, i.e. the clipper's gain-vs-drive law
  hardclip_fund_db()/fit_threshold()   the analytic hard-clip fundamental gain
                    and a one-parameter fit of the ceiling to that law
  shelf_from_sweep()   the pre-emphasis, from how much earlier each sweep
                    frequency reaches the same ceiling
  shelf_from_harmonics()   the pre-emphasis at 3f, 5f ... from how far the
                    measured harmonics of the clipped ramp sit below the
                    analytic hard-clip harmonics (the de-emphasis after the
                    clip is the only thing that can put them there)
  harmonics()       H1..Hn and the aliases of the harmonics above Nyquist
"""
import numpy as np
from scipy.optimize import brentq, least_squares

from .util import seg, cut, db

DEVICE_RATE = 39063.83     # docs/sdly-clock-2026-09-16.md


def amp_at(x, fs, f0, tol=25.0):
    """Amplitude of the component nearest f0 (Hann-windowed, amplitude-
    normalised, so a sine of amplitude A reads A)."""
    n = len(x)
    w = np.hanning(n)
    X = np.abs(np.fft.rfft(x * w)) / (np.sum(w) / 2)
    f = np.fft.rfftfreq(n, 1.0 / fs)
    m = (f > f0 - tol) & (f < f0 + tol)
    return float(X[m].max()) if m.any() else 0.0


def hardclip_fund_db(over_db):
    """Fundamental gain (dB) of an ideal hard clipper for a sine whose
    amplitude is over_db above the threshold. Zero below it."""
    A = np.maximum(10.0 ** (np.asarray(over_db, float) / 20.0), 1e-9)
    r = np.minimum(1.0 / A, 1.0)
    g = (2 / np.pi) * (np.arcsin(r) + r * np.sqrt(np.maximum(1 - r * r, 0.0)))
    return 20 * np.log10(np.where(A <= 1.0, 1.0, g))


def hardclip_harmonics(A, nmax=25, n_fft=1 << 16):
    """Harmonic amplitudes 0..nmax of clip(A*sin, -1, 1)."""
    th = np.arange(n_fft) / n_fft * 2 * np.pi
    Y = np.abs(np.fft.rfft(np.clip(A * np.sin(th), -1.0, 1.0))) * 2 / n_fft
    return Y[:nmax + 1]


def gain_vs_level(cap_max, cap_lin, fs, layout, level_db=14.0509, win=0.08, step=0.1, f0=1000.0):
    """Rows (t, unclipped_dbfs, measured_dbfs, compression_db) for the ramp,
    one per `step` seconds. `unclipped` is the LIN fundamental plus level_db:
    what the MAX output would be if the stage were linear."""
    s = seg(layout, "ramp")
    rows = []
    t = 0.0
    while t < (s["end"] - s["start"]) - win:
        a = cut(cap_max, fs, s["start"] + t, s["start"] + t + win)
        b = cut(cap_lin, fs, s["start"] + t, s["start"] + t + win)
        fa, fb = db(amp_at(a, fs, f0, 40.0)), db(amp_at(b, fs, f0, 40.0))
        rows.append((t, fb + level_db, fa, fa - fb - level_db))
        t += step
    return rows


def fit_threshold(rows, lo_db=-12.0):
    """One-parameter fit of the hard-clip law to gain_vs_level() rows above
    lo_db. Returns (threshold_dbfs, rms_error_db) in the capture's own scale."""
    U = np.array([r[1] for r in rows])
    C = np.array([r[3] for r in rows])
    m = U > lo_db

    def resid(p):
        return hardclip_fund_db(U[m] - p[0]) - C[m]

    sol = least_squares(resid, [U[m].mean() - 4.0])
    return float(sol.x[0]), float(np.sqrt(np.mean(resid(sol.x) ** 2)))


def shelf_from_sweep(cap_max, cap_lin, fs, layout, thr_db, level_db=14.0509,
                     win=0.030, step=0.025, min_comp=0.03, f_min=1000.0):
    """Rows (f, unclipped_amp_dbfs, compression_db, over_db, shelf_db) from the
    sweep: at a fixed level, each frequency's compression says how far above
    the ceiling the pre-emphasis put it, and shelf_db is that distance minus
    the 1 kHz one."""
    s = seg(layout, "sweep")
    f0, f1 = float(s.get("f0", 20.0)), float(s.get("f1", 19000.0))
    sec = s["end"] - s["start"]
    k = np.log(f1 / f0)
    out = []
    t = 0.02
    while t < sec - win - 0.005:
        f = f0 * np.exp(t * k / sec)
        a = cut(cap_max, fs, s["start"] + t, s["start"] + t + win)
        b = cut(cap_lin, fs, s["start"] + t, s["start"] + t + win)
        ra, rb = db(np.sqrt(np.mean(a ** 2))), db(np.sqrt(np.mean(b ** 2)))
        comp = (ra - rb) - level_db
        if comp < -min_comp and f > f_min:
            U = rb + level_db + 20 * np.log10(np.sqrt(2.0))
            over = brentq(lambda o: float(hardclip_fund_db(o)) - comp, 0.0, 40.0)
            out.append((f, U, comp, over, thr_db - (U - over)))
        t += step
    return out


def shelf_from_harmonics(cap_max, cap_lin, fs, layout, thr_db, level_db=14.0509,
                         offsets=(3.7, 3.9, 4.1, 4.3, 4.5, 4.7), win=0.25,
                         f0=1000.0, nmax=19):
    """{harmonic_order: (mean_shelf_db, std, n)} from the ramp: the measured
    harmonic minus the analytic hard-clip harmonic at the same drive is the
    de-emphasis at that frequency, so its negative is the pre-emphasis.
    Windows where the analytic harmonic is within 12 dB of one of its nulls
    are dropped -- the ratio there is meaningless."""
    s = seg(layout, "ramp")
    thr_lin = 10.0 ** (thr_db / 20.0)
    acc = {}
    for toff in offsets:
        t0 = s["start"] + toff - win / 2
        aM = cut(cap_max, fs, t0, t0 + win)
        aL = cut(cap_lin, fs, t0, t0 + win)
        U = db(amp_at(aL, fs, f0, 40.0)) + level_db
        A = 10.0 ** ((U - thr_db) / 20.0)
        th = hardclip_harmonics(A, nmax)
        env = max(th[3], 1e-12)
        for n in range(3, nmax + 1, 2):
            pred = th[n] * thr_lin
            if pred < env * thr_lin * 10 ** (-40 / 20.0):
                continue
            # skip near-null windows: a hard clipper's harmonics have deep
            # nulls in drive, and the measured/analytic ratio is meaningless
            # there. Compare each odd harmonic with its odd neighbours only
            # (n-2 for n=3 is the fundamental, which is never a neighbour).
            nb = [th[m] for m in (n - 2, n + 2) if 3 <= m <= nmax]
            if nb and th[n] < 0.35 * max(nb):
                continue
            d = db(amp_at(aM, fs, f0 * n, 25.0)) - db(pred)
            acc.setdefault(n, []).append(-d)
    return {n: (float(np.mean(v)), float(np.std(v)), len(v)) for n, v in sorted(acc.items())}


def harmonics(x, fs, f0=1000.0, nmax=19, alias_orders=(21, 23, 25, 27, 29, 31),
              device_rate=DEVICE_RATE, tol=25.0):
    """{('H', n): dbfs} for n=1..nmax and {('A', n): dbfs} for each harmonic
    above the device's Nyquist, read at the frequency it folds back to."""
    out = {}
    for n in range(1, nmax + 1):
        out[("H", n)] = db(amp_at(x, fs, f0 * n, tol))
    for n in alias_orders:
        out[("A", n)] = db(amp_at(x, fs, device_rate - f0 * n, tol))
    return out


def shelf_model_db(f, tau_zero_us, tau_pole_us):
    w = 2 * np.pi * np.asarray(f, float)
    return 10 * np.log10((1 + (w * tau_zero_us * 1e-6) ** 2) / (1 + (w * tau_pole_us * 1e-6) ** 2))


def fit_shelf(points, guess=(50.0, 15.0)):
    """points: (f, shelf_db_relative_to_1kHz, weight). Returns
    (tau_zero_us, tau_pole_us, rms_err_db)."""
    F = np.array([p[0] for p in points])
    Y = np.array([p[1] for p in points])
    W = np.array([p[2] for p in points])

    def resid(p):
        return (shelf_model_db(F, p[0], p[1]) - shelf_model_db(1000.0, p[0], p[1]) - Y) * W

    sol = least_squares(resid, list(guess), bounds=([1.0, 1.0], [500.0, 200.0]))
    return float(sol.x[0]), float(sol.x[1]), float(np.sqrt(np.mean(resid(sol.x) ** 2)))
