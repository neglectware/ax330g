#!/usr/bin/env python3
"""Generate the fixed test-signal set (one WAV, fixed layout) and layout.json.

Layout (seconds, 48 kHz mono, -20 dBFS reference unless noted). Every segment
is followed by a gap so delay tails have somewhere to go. The layout file is
what the analysis reads, so never rely on the numbers below directly.

  silence   2 s                      noise floor
  clicks    single-sample clicks (0.158 FS = -16 dBFS; 0.5 until 2026-09-13) at 0, 2.0, 4.3, 7.0 s (irregular so
            they do not all land on the same LFO phase)
  bursts    1 kHz, 60 ms bursts, -20 dBFS, same irregular spacing: isolated
            repeats for per-repeat pitch (LFO inside vs after the loop)
  sine20    1 kHz, -20 dBFS, 4 s     level/THD/quantization reference
  sine40    1 kHz, -40 dBFS, 2 s     quantization vs level (companding test)
  ramp      1 kHz, -40 -> 0 dBFS, 5 s   clip onset (M2 only; --no-ramp drops it)
  sweep     log sine 20 Hz-19 kHz, 5 s, -20 dBFS
  noise     white, 3 s, -30 dBFS (was -20 until 2026-09-13)
  di        clean DI guitar, 8 s (capture/di_clip.wav if present, else a
            synthesized placeholder so the pipeline can be exercised)

--tail-set (build_tail_set()): high-feedback measurement set, for loops
whose decay outruns the normal set's inter-segment gaps (e.g. the owner's
Etherbunny patch: Stereo Mod Delay at Fb 46, ~0.92/repeat, 20+ s tail). 2 s
silence, then a single click / a single -20 dBFS 1 kHz burst / a single
-6 dBFS "hot" burst (same level, to exercise 16-bit truncation and any
in-loop saturation) / a 4 s -20 dBFS 1 kHz tone, EACH followed by a 28 s
silent tail folded into that same segment -- so the whole decay is inside
one named span and null_test's per-segment table covers all of it, not just
the onset that would otherwise bleed into the inter-segment gap. No sweep
(analysis/align.py falls back to sine20, present here, then clicks) and no
ramp (null_test masks a "ramp" segment only when one is present). About
125 s total; see the Makefile's signals-tail target for the
signalset-tail.wav / layout-tail.json copy convention.

--rev-set (build_rev_set()): hot broadband excitation for the reverb tank,
per docs/rev-identification-2026-09-17.md section 8 -- the tail set's
broadest signal is a -16 dBFS click, whose impulse response only has 28 dB
of range, which is what stopped the structural identification short of the
tank. This set puts a -6 dBFS click, a -6 dBFS 5 s log sweep and a -6 dBFS
1 kHz burst (same shape as the tail set's burst_hot, for continuity) each
ahead of its own 28 s silent tail, so the whole loop structure sits above
the noise floor per channel. Capture at Balance 50 (all wet, no dry path to
deconvolve around). No ramp/noise/DI/sine20/sine40/plain clicks-at-normal-
level. See the Makefile's signals-rev target for the signalset-rev.wav /
layout-rev.json copy convention.
"""
import argparse
import json
import os
import numpy as np
import soundfile as sf

FS = 48000
HERE = os.path.dirname(os.path.abspath(__file__))


def db(x):
    return 10 ** (x / 20.0)


def fade(x, ms=5.0, fs=FS):
    n = int(ms * fs / 1000)
    if n and len(x) > 2 * n:
        w = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, n))
        x = x.copy()
        x[:n] *= w
        x[-n:] *= w[::-1]
    return x


def sine(f, sec, amp, fs=FS):
    t = np.arange(int(sec * fs)) / fs
    return fade(amp * np.sin(2 * np.pi * f * t))


def log_sweep(f0, f1, sec, amp, fs=FS):
    t = np.arange(int(sec * fs)) / fs
    k = np.log(f1 / f0)
    ph = 2 * np.pi * f0 * sec / k * (np.exp(t * k / sec) - 1)
    return fade(amp * np.sin(ph))


def noise(sec, amp, fs=FS, seed=1):
    rng = np.random.default_rng(seed)
    x = rng.standard_normal(int(sec * fs))
    x /= np.sqrt(np.mean(x ** 2))
    return fade(amp * x)


def di_clip(sec, fs=FS):
    p = os.path.join(HERE, "di_clip.wav")
    if os.path.exists(p):
        x, f = sf.read(p, dtype="float64", always_2d=True)
        x = x[:, 0]
        if f != fs:
            from scipy.signal import resample_poly
            from fractions import Fraction
            fr = Fraction(fs, int(f))
            x = resample_poly(x, fr.numerator, fr.denominator)
        x = x[: int(sec * fs)]
        out = np.zeros(int(sec * fs))
        out[: len(x)] = x
        return out, True
    # placeholder: Karplus-Strong plucks over a chord sequence
    rng = np.random.default_rng(7)
    out = np.zeros(int(sec * fs))
    chords = [[82.41, 123.47, 164.81, 207.65, 246.94, 329.63],
              [110.0, 164.81, 220.0, 277.18, 329.63, 440.0],
              [146.83, 220.0, 293.66, 369.99, 440.0, 587.33],
              [98.0, 146.83, 196.0, 246.94, 293.66, 392.0]]
    t0 = 0.2
    for c in range(len(chords)):
        for strum in range(2):
            for j, f in enumerate(chords[c]):
                start = int((t0 + j * 0.012) * fs)
                n = int(1.6 * fs)
                if start + n > len(out):
                    break
                N = int(round(fs / f))
                buf = rng.uniform(-1, 1, N)
                y = np.zeros(n)
                for i in range(n):
                    y[i] = buf[i % N]
                    buf[i % N] = 0.996 * 0.5 * (buf[i % N] + buf[(i + 1) % N])
                y *= 0.35 * (0.8 if strum else 1.0)
                out[start:start + n] += y
            t0 += 0.9
    peak = np.max(np.abs(out))
    return out * (db(-8) / peak), False


def build_lfo_set(click_amp=0.158, tone_s=30.0):
    """LFO measurement set: 2 s silence, the four clicks, a 30 s 1 kHz tone at
    -20 dBFS (the pitch track of a long tone gives LFO rate, shape, stepping
    and centring over many cycles), then the burst segment."""
    segs = []
    parts = []
    t = 0.0

    def add(name, x, gap, meta=None):
        nonlocal t
        d = {"name": name, "start": round(t, 6), "end": round(t + len(x) / FS, 6)}
        if meta:
            d.update(meta)
        segs.append(d)
        parts.append(x)
        parts.append(np.zeros(int(gap * FS)))
        t += len(x) / FS + gap

    add("silence", np.zeros(int(2.0 * FS)), 0.0)
    times = [0.0, 2.0, 4.3, 7.0]
    clicks = np.zeros(int(8.6 * FS))
    for tt in times:
        clicks[int(tt * FS)] = click_amp
    add("clicks", clicks, 1.0, {"click_times": [round(2.0 + tt, 6) for tt in times], "click_amp": click_amp})
    add("sine20", sine(1000, tone_s, db(-20)), 1.5, {"hz": 1000, "dbfs": -20})
    bt = [0.0, 2.0, 4.3, 7.0]
    bursts = np.zeros(int(8.6 * FS))
    for tt in bt:
        i = int(tt * FS)
        bursts[i:i + int(0.06 * FS)] = fade(sine(1000, 0.06, db(-20)))
    b0 = segs[-1]["end"] + 1.5
    add("bursts", bursts, 1.0, {"burst_times": [round(b0 + tt, 6) for tt in bt], "hz": 1000, "dbfs": -20, "len_s": 0.06})
    x = np.concatenate(parts)
    layout = {"fs": FS, "duration": round(len(x) / FS, 6), "segments": segs, "variant": "lfo"}
    return x, layout


def build_tail_set(click_amp=0.158, tail_s=28.0, tone_s=4.0):
    """High-feedback tail set: one click, one -20 dBFS burst, one -6 dBFS
    "hot" burst and a short tone, each followed by `tail_s` seconds of
    silence folded into that segment's own span -- so a slow decay (Fb ~46-50
    on SMOD/SDLY, 20+ s) has somewhere to go without running into the next
    segment, and null_test's per-segment table (which cuts exactly to each
    segment's [start, end]) reports the whole decay rather than just the
    onset. No sweep -- analysis/align.py's find_offset() falls back to
    sine20 (present here) when "sweep" is not in the layout's segment
    names."""
    segs = []
    parts = []
    t = 0.0

    def add(name, x, gap, meta=None):
        nonlocal t
        d = {"name": name, "start": round(t, 6), "end": round(t + len(x) / FS, 6)}
        if meta:
            d.update(meta)
        segs.append(d)
        parts.append(x)
        parts.append(np.zeros(int(gap * FS)))
        t += len(x) / FS + gap

    add("silence", np.zeros(int(2.0 * FS)), 0.0)

    n_tail = int(round(tail_s * FS))
    blen = int(0.06 * FS)

    start = t
    click = np.zeros(n_tail)
    click[0] = click_amp
    add("clicks", click, 1.5, {"click_times": [round(start, 6)], "click_amp": click_amp, "tail_s": tail_s})

    start = t
    bursts = np.zeros(n_tail)
    bursts[:blen] = fade(sine(1000, 0.06, db(-20)), 3.0)
    add("bursts", bursts, 1.5, {"burst_times": [round(start, 6)], "hz": 1000, "dbfs": -20, "len_s": 0.06, "tail_s": tail_s})

    start = t
    bursts_hot = np.zeros(n_tail)
    bursts_hot[:blen] = fade(sine(1000, 0.06, db(-6)), 3.0)
    add("burst_hot", bursts_hot, 1.5,
        {"burst_times": [round(start, 6)], "hz": 1000, "dbfs": -6, "len_s": 0.06, "tail_s": tail_s,
         "note": "same level/shape as bursts but -6 dBFS, to exercise 16-bit truncation and any in-loop saturation"})

    tone = sine(1000, tone_s, db(-20))
    sine_tail = np.zeros(len(tone) + n_tail)
    sine_tail[:len(tone)] = tone
    add("sine20", sine_tail, 2.5, {"hz": 1000, "dbfs": -20, "tone_s": tone_s, "tail_s": tail_s})

    x = np.concatenate(parts)
    layout = {"fs": FS, "duration": round(len(x) / FS, 6), "segments": segs, "variant": "tail"}
    return x, layout


def build_rev_set(click_amp=None, tail_s=28.0, sweep_s=5.0):
    """Hot broadband tail set for the reverb (docs/rev-identification-2026-09-17.md
    section 8): a -6 dBFS click, a -6 dBFS 5 s log sweep (reuses log_sweep's
    own fade) and a -6 dBFS 1 kHz burst (same shape as build_tail_set's
    burst_hot, for continuity with the existing analysis), each followed by
    `tail_s` seconds of silence folded into that segment's own span so a
    Rev Time 10 decay has somewhere to go. -6 dBFS (vs the tail set's -16
    dBFS click) puts the entire loop structure -- not just its loudest 28 dB
    -- above the capture noise floor, per channel. Capture with Balance 50
    (all wet, so there is no dry path to deconvolve around). No
    ramp/noise/DI/sine20/sine40/normal-level clicks."""
    click_amp = db(-6) if click_amp is None else click_amp
    segs = []
    parts = []
    t = 0.0

    def add(name, x, gap, meta=None):
        nonlocal t
        d = {"name": name, "start": round(t, 6), "end": round(t + len(x) / FS, 6)}
        if meta:
            d.update(meta)
        segs.append(d)
        parts.append(x)
        parts.append(np.zeros(int(gap * FS)))
        t += len(x) / FS + gap

    add("silence", np.zeros(int(2.0 * FS)), 0.0)

    n_tail = int(round(tail_s * FS))
    blen = int(0.06 * FS)

    start = t
    click = np.zeros(n_tail)
    click[0] = click_amp
    add("clicks", click, 1.5, {"click_times": [round(start, 6)], "click_amp": click_amp, "tail_s": tail_s})

    start = t
    sweep = log_sweep(20, 19000, sweep_s, db(-6))
    sweep_tail = np.zeros(len(sweep) + n_tail)
    sweep_tail[:len(sweep)] = sweep
    add("sweep", sweep_tail, 1.5, {"f0": 20, "f1": 19000, "dbfs": -6, "sweep_s": sweep_s, "tail_s": tail_s})

    start = t
    bursts_hot = np.zeros(n_tail)
    bursts_hot[:blen] = fade(sine(1000, 0.06, db(-6)), 3.0)
    add("burst_hot", bursts_hot, 1.5,
        {"burst_times": [round(start, 6)], "hz": 1000, "dbfs": -6, "len_s": 0.06, "tail_s": tail_s})

    x = np.concatenate(parts)
    layout = {"fs": FS, "duration": round(len(x) / FS, 6), "segments": segs, "variant": "rev"}
    return x, layout


def build(no_ramp=False, click_amp=0.158, noise_dbfs=-30.0, di_peak_dbfs=-18.0):
    segs = []
    parts = []
    t = 0.0

    def add(name, x, gap, meta=None):
        nonlocal t
        d = {"name": name, "start": round(t, 6), "end": round(t + len(x) / FS, 6)}
        if meta:
            d.update(meta)
        segs.append(d)
        parts.append(x)
        parts.append(np.zeros(int(gap * FS)))
        t += (len(x) + int(gap * FS)) / FS

    add("silence", np.zeros(2 * FS), 0.0)
    offsets = [0.0, 2.0, 4.3, 7.0]
    clicks = np.zeros(int(8.6 * FS))
    times = []
    for o in offsets:
        i = int(o * FS)
        clicks[i] = click_amp
        times.append(round(t + i / FS, 6))
    add("clicks", clicks, 1.0, {"click_times": times, "click_amp": click_amp})
    bursts = np.zeros(int(8.6 * FS))
    times = []
    blen = int(0.06 * FS)
    for o in offsets:
        i = int(o * FS)
        bursts[i:i + blen] = fade(sine(1000, 0.06, db(-20)), 3.0)
        times.append(round(t + i / FS, 6))
    add("bursts", bursts, 1.0, {"burst_times": times, "hz": 1000, "dbfs": -20, "len_s": 0.06})
    add("sine20", sine(1000, 4, db(-20)), 1.5, {"hz": 1000, "dbfs": -20})
    add("sine40", sine(1000, 2, db(-40)), 1.5, {"hz": 1000, "dbfs": -40})
    if not no_ramp:
        n = 5 * FS
        tt = np.arange(n) / FS
        amp = db(-40 + 40 * tt / 5.0)
        add("ramp", fade(amp * np.sin(2 * np.pi * 1000 * tt)), 1.5, {"hz": 1000, "dbfs_from": -40, "dbfs_to": 0})
    add("sweep", log_sweep(20, 19000, 5, db(-20)), 1.5, {"f0": 20, "f1": 19000, "dbfs": -20})
    add("noise", noise(3, db(noise_dbfs)), 1.5, {"dbfs": noise_dbfs})
    di, real = di_clip(8)
    pk = np.abs(di).max()
    if pk > 0:
        di = di * (db(di_peak_dbfs) / pk)
    add("di", di, 1.0, {"real_clip": real, "peak_dbfs": di_peak_dbfs})
    x = np.concatenate(parts)
    layout = {"fs": FS, "duration": round(len(x) / FS, 6), "segments": segs}
    return x, layout


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(HERE, "signalset.wav"))
    ap.add_argument("--layout", default=os.path.join(HERE, "layout.json"))
    ap.add_argument("--no-ramp", action="store_true")
    ap.add_argument("--lfo-set", action="store_true",
                    help="alternate set for LFO measurement: clicks, then a 30 s 1 kHz tone, then bursts (no ramp/sweep/noise/DI)")
    ap.add_argument("--tone-seconds", type=float, default=30.0,
                    help="LFO set only: length of the 1 kHz tone (120 s for the 0.02-0.18 Hz speeds, whose periods are 5.6-50 s)")
    ap.add_argument("--tail-set", action="store_true",
                    help="high-feedback set: single click/burst/hot-burst/tone, each with a 28 s silent tail "
                         "(no sweep/ramp/noise/DI/sine40) -- for Fb ~46-50 loops whose decay outruns the normal "
                         "set's gaps")
    ap.add_argument("--tail-seconds", type=float, default=28.0,
                    help="--tail-set/--rev-set: length of the silent tail after each single event")
    ap.add_argument("--rev-set", action="store_true",
                    help="hot broadband set for the reverb tank: -6 dBFS click/sweep/burst, each with a 28 s "
                         "silent tail (no ramp/noise/DI/sine20/sine40) -- see "
                         "docs/rev-identification-2026-09-17.md section 8")
    ap.add_argument("--sweep-seconds", type=float, default=5.0,
                    help="--rev-set only: length of the log sweep")
    ap.add_argument("--variant", choices=["normal", "lfo", "tail", "rev"], default=None,
                    help="alternate spelling of --lfo-set/--tail-set/--rev-set (normal = none of them)")
    a = ap.parse_args()
    if a.variant == "lfo":
        a.lfo_set = True
    elif a.variant == "tail":
        a.tail_set = True
    elif a.variant == "rev":
        a.rev_set = True
    if a.lfo_set:
        x, layout = build_lfo_set(tone_s=a.tone_seconds)
    elif a.tail_set:
        x, layout = build_tail_set(tail_s=a.tail_seconds)
    elif a.rev_set:
        x, layout = build_rev_set(tail_s=a.tail_seconds, sweep_s=a.sweep_seconds)
    else:
        x, layout = build(a.no_ramp)
    sf.write(a.out, x.astype(np.float32), FS, subtype="PCM_24")
    with open(a.layout, "w") as f:
        json.dump(layout, f, indent=1)
    print(f"wrote {a.out} ({layout['duration']:.1f} s) and {a.layout}")
    for s in layout["segments"]:
        print(f"  {s['name']:8s} {s['start']:7.2f} - {s['end']:7.2f}")


if __name__ == "__main__":
    main()
