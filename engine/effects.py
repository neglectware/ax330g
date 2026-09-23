"""Effect renderers. Each takes the device-rate input (N,2), the spec, fs and
returns (output (N,2), truth) where truth is the resolved physical parameters
the analysis is expected to recover.
"""
import math
import numpy as np
from .blocks import DelayLine, Storage, OnePoleLP, Gain, LFO
from .params import apply_map


def _fbgain(spec, coef):
    b = spec.get("blocks", {}).get("feedback_gain", {})
    return Gain(coef, b.get("wordlength"), b.get("rounding", "truncate"), b.get("overflow", "saturate"))


def _delay_line(spec, fs):
    b = spec.get("blocks", {}).get("delay_line", {})
    st = Storage(b.get("storage"))
    max_samples = int(b.get("max_ms", 1000) * fs / 1000.0) + 64
    return DelayLine(max_samples, st, b.get("interp", "none")), st


def _damp(spec, fs, hz):
    b = spec.get("blocks", {}).get("damp", {})
    return OnePoleLP(fs, hz, b.get("wordlength"), b.get("rounding", "truncate"))


def render_sdly(x, spec, fs):
    """Stereo Delay, routing (e): independent per channel, own balance.

    blocks.damp.position: "feedback" (filter only in the feedback path; the
    first repeat is unfiltered) or "input" (filter on the line's write, so
    every repeat carries one more pass; measured on the AX30G 2026-09-13).
    blocks.delay_line.wet_offset_samples: extra fixed delay on the wet output
    only (the AX30G's first repeat lands 12 samples later than the loop
    spacing predicts).
    blocks.delay_line.input_hpf_hz: first-order high-pass on the line WRITE
    (inside the loop, so repeat j carries j passes).
    blocks.wet_hpf_hz: first-order high-pass on the wet READ, outside the
    loop (applied once, whatever the feedback). The 2026-09-13 chain fit
    found the wet path's extra LF phase fits this placement better.
    maps.dry (optional): dry gain vs Balance; default 1 - wet.
    """
    p = spec["params"]
    m = spec["maps"]
    b = spec.get("blocks", {})
    damp_pos = b.get("damp", {}).get("position", "feedback")
    wet_off = int(b.get("delay_line", {}).get("wet_offset_samples", 0))
    hpf_hz = b.get("delay_line", {}).get("input_hpf_hz")   # DC blocker on the line input (AX30G: ~6.5 Hz)
    hp_a = math.exp(-2.0 * math.pi * hpf_hz / fs) if hpf_hz else None
    wet_hpf_hz = b.get("wet_hpf_hz")
    wh_a = math.exp(-2.0 * math.pi * wet_hpf_hz / fs) if wet_hpf_hz else None
    n = x.shape[0]
    y = np.zeros_like(x)
    truth = {"effect": "SDLY", "channels": []}
    for ch, side in enumerate(("L", "R")):
        D = apply_map(m["delay"], p[f"{side} Dly"], fs)
        fb = apply_map(m["feedback"], p[f"{side} Fb"], fs)
        damp_hz = apply_map(m["high_damp"], p["High Damp"], fs)
        wet = apply_map(m["balance"], p[f"{side} Bal"], fs)
        dry = apply_map(m["dry"], p[f"{side} Bal"], fs) if "dry" in m else 1.0 - wet
        dl, st = _delay_line(spec, fs)
        lp = _damp(spec, fs, damp_hz)
        g = _fbgain(spec, fb)
        xin = x[:, ch].tolist()
        out = [0.0] * n
        hist = [0.0] * (wet_off + 1)   # wet-output pipeline
        hp_x1 = hp_y1 = 0.0
        wh_x1 = wh_y1 = 0.0
        for i in range(n):
            xi = xin[i]
            r = dl.read(D)
            if damp_pos == "input":
                w = xi + g(r)
            else:
                w = xi + g(lp(r))
            if hp_a is not None:      # y = x - x1 + a*y1 (first-order high-pass)
                hy = w - hp_x1 + hp_a * hp_y1
                hp_x1, hp_y1 = w, hy
                w = hy
            if w > 0.999969:
                w = 0.999969
            elif w < -1.0:
                w = -1.0
            dl.write(lp(w) if damp_pos == "input" else w)
            if wet_off:
                hist.append(r)
                r = hist.pop(0)
            if wh_a is not None:      # wet-path high-pass, outside the loop
                wy = r - wh_x1 + wh_a * wh_y1
                wh_x1, wh_y1 = r, wy
                r = wy
            out[i] = dry * xi + wet * r
        y[:, ch] = out
        truth["channels"].append({
            "delay_ms": D * 1000.0 / fs, "delay_samples": D, "fb_coef": fb,
            "damp_hz": damp_hz, "wet": wet, "dry": dry,
            "storage": vars(st),
        })
    return y, truth


def render_modd(x, spec, fs):
    """Modulation Delay, routing (c): L+R summed, one effect, per-side balance.

    blocks.lfo.in_loop decides whether the modulated read is inside the
    feedback loop (pitch wobble compounds per repeat) or only on the output.
    """
    p = spec["params"]
    m = spec["maps"]
    n = x.shape[0]
    D = apply_map(m["delay"], p["Dly Time"], fs)
    fb = apply_map(m["feedback"], p["Feedback"], fs)
    damp_hz = apply_map(m["high_damp"], p["High Damp"], fs)
    rate = apply_map(m["speed"], p["Speed"], fs)
    depth_ms = apply_map(m["depth"], p["Depth"], fs)
    depth = depth_ms * fs / 1000.0
    wetL = apply_map(m["balance"], p["L Bal"], fs)
    wetR = apply_map(m["balance"], p["R Bal"], fs)
    lcfg = spec.get("blocks", {}).get("lfo", {})
    lfo = LFO(fs, rate, lcfg.get("shape", "tri"), lcfg.get("update_every", 1), lcfg.get("phase", 0.0), lcfg.get("table"))
    in_loop = bool(lcfg.get("in_loop", False))
    dl, st = _delay_line(spec, fs)
    lp = _damp(spec, fs, damp_hz)
    g = _fbgain(spec, fb)
    xin = ((x[:, 0] + x[:, 1]) * 0.5).tolist()
    outL = [0.0] * n
    outR = [0.0] * n
    dryL, dryR = 1.0 - wetL, 1.0 - wetR
    base = D + depth  # keep the modulated read >= D
    for i in range(n):
        xi = xin[i]
        mod = base + depth * lfo()
        if in_loop:
            r = dl.read(mod)
            v = g(lp(r))
        else:
            r = dl.read(mod)
            v = g(lp(dl.read(base)))
        w = xi + v
        if w > 0.999969:
            w = 0.999969
        elif w < -1.0:
            w = -1.0
        dl.write(w)
        outL[i] = dryL * xi + wetL * r
        outR[i] = dryR * xi + wetR * r
    y = np.zeros_like(x)
    y[:, 0] = outL
    y[:, 1] = outR
    truth = {
        "effect": "MODD", "delay_ms": base * 1000.0 / fs, "delay_samples": base,
        "fb_coef": fb, "damp_hz": damp_hz, "wet": [wetL, wetR],
        "lfo_hz": rate, "lfo_shape": lfo.shape, "lfo_update_every": lfo.upd,
        "depth_ms": depth_ms, "in_loop": in_loop, "storage": vars(st),
    }
    return y, truth


def render_smod(x, spec, fs):
    """Stereo Modulation Delay, routing (e): two independent delay lines with
    their own Dly / Feedback / Balance, one LFO shared, R fed the INVERTED
    LFO (measured 2026-09-14: R = -L, best time shift 0.00 ms). Input is the
    mono sum like MODD (the unit has one guitar input). Modulation is
    unipolar above nominal and inside the loop, as in MODD. No High Damp
    parameter on the unit; blocks.damp_hz (optional) is a fixed one-pole on
    the line write if a fixed damping is ever measured.
    blocks.delay_line.offset_samples: fixed addition to the per-side delay
    (the Depth-0 capture reads 100.15 ms for a 100 ms setting: 39/ms + 12).
    """
    p = spec["params"]
    m = spec["maps"]
    b = spec.get("blocks", {})
    n = x.shape[0]
    rate = apply_map(m["speed"], p["Speed"], fs)
    depth_ms = apply_map(m["depth"], p["Depth"], fs)
    depth = depth_ms * fs / 1000.0
    lcfg = b.get("lfo", {})
    lfo = LFO(fs, rate, lcfg.get("shape", "table"), lcfg.get("update_every", 1), lcfg.get("phase", 0.0), lcfg.get("table"))
    in_loop = bool(lcfg.get("in_loop", True))
    sign = lcfg.get("side_sign", [1.0, -1.0])
    off = int(b.get("delay_line", {}).get("offset_samples", 0))
    damp_hz = b.get("damp_hz")
    xin = ((x[:, 0] + x[:, 1]) * 0.5).tolist()
    y = np.zeros_like(x)
    truth = {"effect": "SMOD", "lfo_hz": rate, "depth_ms": depth_ms, "in_loop": in_loop, "channels": []}
    lines = []
    for ch, side in enumerate(("L", "R")):
        D = apply_map(m["delay"], p[f"{side} Dly"], fs) + off
        fb = apply_map(m["feedback"], p[f"{side} Fb"], fs)
        wet = apply_map(m["balance"], p[f"{side} Bal"], fs)
        dry = apply_map(m["dry"], p[f"{side} Bal"], fs) if "dry" in m else 1.0 - wet
        dl, st = _delay_line(spec, fs)
        lp = OnePoleLP(fs, damp_hz) if damp_hz else None
        g = _fbgain(spec, fb)
        lines.append((D, fb, wet, dry, dl, lp, g, [0.0] * n, sign[ch]))
        truth["channels"].append({"delay_ms": D * 1000.0 / fs, "delay_samples": D, "fb_coef": fb, "wet": wet, "dry": dry, "storage": vars(st)})
    for i in range(n):
        xi = xin[i]
        l = lfo()
        for (D, fb, wet, dry, dl, lp, g, out, sg) in lines:
            base = D + depth
            mod = base + depth * (sg * l)
            if in_loop:
                r = dl.read(mod)
                v = g(r)
            else:
                r = dl.read(mod)
                v = g(dl.read(base))
            w = xi + v
            if w > 0.999969:
                w = 0.999969
            elif w < -1.0:
                w = -1.0
            dl.write(lp(w) if lp else w)
            out[i] = dry * xi + wet * r
    y[:, 0] = lines[0][7]
    y[:, 1] = lines[1][7]
    return y, truth


def render_cho(x, spec, fs):
    """Chorus (Mod1 block), mono in / mono out, measured 2026-09-16.

    One modulated tap added to the dry signal at a fixed mix; no feedback,
    no damping, no delay-time parameter. The delay is a fixed 939 device
    samples (24.038 ms) and the modulation is UNIPOLAR ABOVE it, exactly as
    MODD/SMOD are: delay(t) = D + depth + depth*lfo(t), lfo in [-1, 1], so
    the range is [D, D + 2*depth] and the minimum is the static delay.

    maps.delay / maps.dry / maps.wet are "constant" maps: the unit gives the
    Chorus only Speed and Depth, so the delay and the mix are device facts,
    not panel parameters. Both outputs carry the same signal (Mod1 is mono;
    L and R measured identical to 0.03 dB with no time offset).
    """
    p = spec["params"]
    m = spec["maps"]
    b = spec.get("blocks", {})
    n = x.shape[0]
    D = apply_map(m["delay"], 0, fs)
    rate = apply_map(m["speed"], p["Speed"], fs)
    depth_ms = apply_map(m["depth"], p["Depth"], fs)
    depth = depth_ms * fs / 1000.0
    wet = apply_map(m["wet"], 0, fs)
    dry = apply_map(m["dry"], 0, fs)
    lcfg = b.get("lfo", {})
    lfo = LFO(fs, rate, lcfg.get("shape", "table"), lcfg.get("update_every", 1),
              lcfg.get("phase", 0.0), lcfg.get("table"))
    dl, st = _delay_line(spec, fs)
    xin = ((x[:, 0] + x[:, 1]) * 0.5).tolist()
    out = [0.0] * n
    base = D + depth          # keep the modulated read >= D
    for i in range(n):
        xi = xin[i]
        r = dl.read(base + depth * lfo())
        w = xi
        if w > 0.999969:
            w = 0.999969
        elif w < -1.0:
            w = -1.0
        dl.write(w)
        out[i] = dry * xi + wet * r
    y = np.zeros_like(x)
    y[:, 0] = out
    y[:, 1] = out
    truth = {
        "effect": "CHO", "delay_samples": D, "delay_ms": D * 1000.0 / fs,
        "mod_range_samples": [D, D + 2 * depth], "depth_ms": depth_ms,
        "lfo_hz": rate, "lfo_shape": lfo.shape, "lfo_update_every": lfo.upd,
        "wet": wet, "dry": dry, "fb_coef": 0.0, "storage": vars(st),
    }
    return y, truth


def render_scho(x, spec, fs):
    """Stereo Chorus (SCHO) -- a what-if stereo chorus in the unit's idiom,
    Mark 2026-09-16 (AX30G_HANDOFF.md Sec 7b). THIS BLOCK NEVER EXISTED ON
    THE HARDWARE. The real Chorus (CHO, render_cho above) is mono, measured
    2026-09-16 (docs/cho-model-2026-09-16.md). Mark, a stereo-chorus user,
    asked for a stereo option built from the same DSP; the design borrows
    Stereo Mod Delay's measured inverted-LFO idiom (one LFO, R fed the
    inverted LFO, best time shift 0.00 ms -- docs/smod-model-2026-09-14.md,
    blocks.lfo.side_sign [1, -1]) rather than inventing a new one.

    Base topology, both modes: ONE shared mono delay line (single write per
    sample, still just the clipped dry input -- no feedback, exactly as
    CHO). DelayLine.read() has no side effects (only write() advances
    state), so reading twice per sample before one write is safe -- the
    same property render_smod relies on for its own two-line version of
    this trick. Speed and Depth are the only modulation parameters; delay,
    mix gains, LFO shape and speed/depth maps are all CHO's, unchanged.

    Mode (p["Mode"], 0 or 1, added 2026-09-16 at Mark's request):

    - **0, "Inverted LFO" (default).** Two independently modulated reads --
      left with +lfo, right with -lfo -- each mixed 0.75 dry / 0.25 wet,
      the design above.
    - **1, "Split" (CE-1 style).** Left is DRY ONLY (`dry * xi`, no wet
      component); right is the single CHO tap WET ONLY (`wet * r`, using
      the SAME `+lfo` tap CHO itself uses -- no sign, not `sign[0] * l`).
      So `L + R` sums to exactly the mono CHO output at the same Speed/
      Depth -- verified in docs/scho-cpp-2026-09-16.md. This is the
      classic Boss CE-1/early chorus "L dry / R wet" split, distinct from
      Mode 0's two-sided modulation.

    Deliberately absent: the inverted-polarity Juno-60/CE-2-style chorus
    (both channels wet and out of phase, no dry anywhere) -- not asked for
    and not built; Mode 0 above already covers a bipolar-feeling stereo
    spread through CHO's own unipolar-above-nominal LFO law, and Mode 1
    covers the classic dry/wet split, which together were what Mark asked
    for on 2026-09-16.

    Open-mode only: never offered in "as the unit" mode (AX30G_HANDOFF.md
    Sec 7b's chain-order rules never include it, since the hardware never
    had it).
    """
    p = spec["params"]
    m = spec["maps"]
    b = spec.get("blocks", {})
    n = x.shape[0]
    D = apply_map(m["delay"], 0, fs)
    rate = apply_map(m["speed"], p["Speed"], fs)
    depth_ms = apply_map(m["depth"], p["Depth"], fs)
    depth = depth_ms * fs / 1000.0
    wet = apply_map(m["wet"], 0, fs)
    dry = apply_map(m["dry"], 0, fs)
    mode = int(p.get("Mode", 0))
    lcfg = b.get("lfo", {})
    lfo = LFO(fs, rate, lcfg.get("shape", "table"), lcfg.get("update_every", 1),
              lcfg.get("phase", 0.0), lcfg.get("table"))
    sign = lcfg.get("side_sign", [1.0, -1.0])
    dl, st = _delay_line(spec, fs)
    xin = ((x[:, 0] + x[:, 1]) * 0.5).tolist()
    outL = [0.0] * n
    outR = [0.0] * n
    base = D + depth          # keep the modulated read >= D
    for i in range(n):
        xi = xin[i]
        l = lfo()
        w = xi
        if w > 0.999969:
            w = 0.999969
        elif w < -1.0:
            w = -1.0
        if mode == 1:
            # Split (CE-1 style): the SAME +lfo tap CHO uses -- no side sign.
            r = dl.read(base + depth * l)
            dl.write(w)
            outL[i] = dry * xi
            outR[i] = wet * r
        else:
            rL = dl.read(base + depth * (sign[0] * l))
            rR = dl.read(base + depth * (sign[1] * l))
            dl.write(w)
            outL[i] = dry * xi + wet * rL
            outR[i] = dry * xi + wet * rR
    y = np.zeros_like(x)
    y[:, 0] = outL
    y[:, 1] = outR
    truth = {
        "effect": "SCHO", "delay_samples": D, "delay_ms": D * 1000.0 / fs,
        "mod_range_samples": [D, D + 2 * depth], "depth_ms": depth_ms,
        "lfo_hz": rate, "lfo_shape": lfo.shape, "lfo_update_every": lfo.upd,
        "wet": wet, "dry": dry, "fb_coef": 0.0, "mode": mode, "storage": vars(st),
    }
    return y, truth


# --------------------------------------------------------------- 3BEQ

def _eq3_lp1(fs, f0):
    """Bilinear one-pole low-pass, unity at DC: (c + c z^-1)/(1 - a z^-1)."""
    T = math.tan(math.pi * f0 / fs)
    a = (1.0 - T) / (1.0 + T)
    c = (1.0 - a) / 2.0
    return [c, c], [1.0, -a]


def _eq3_hp1(fs, f0):
    """Bilinear one-pole high-pass, unity at Nyquist: (d - d z^-1)/(1 - a z^-1)."""
    T = math.tan(math.pi * f0 / fs)
    a = (1.0 - T) / (1.0 + T)
    d = (1.0 + a) / 2.0
    return [d, -d], [1.0, -a]


def _eq3_bp2(fs, fc, q, alpha_law="w0"):
    """Constant-peak-gain band-pass, unity at fc.  alpha_law picks how the
    bandwidth coefficient is formed: "w0" is alpha = w0/(2Q) (what the unit
    measures as -- docs/3beq-model-2026-09-17.md), "sin" is RBJ's own
    alpha = sin(w0)/(2Q)."""
    w0 = 2.0 * math.pi * fc / fs
    al = (w0 if alpha_law == "w0" else math.sin(w0)) / (2.0 * q)
    cw = math.cos(w0)
    a0 = 1.0 + al
    return [al / a0, 0.0, -al / a0], [1.0, -2.0 * cw / a0, (1.0 - al) / a0]


def _eq3_section(b, a, gdb, off_boost, off_cut, allpass_flat):
    """One band of the 3BEQ, as measured 2026-09-17 (complex response, not
    just magnitude -- docs/3beq-model-2026-09-17.md).

    b/a is the band's FIXED filter F (unity at the band's own end of the
    spectrum, zero at the other).  G = 10^(|dB|/20).

      boost, allpass_flat:   S = 1 - 2*g*F,  g = (1 + G)/2   (NON-minimum
                             phase: the zero is outside the unit circle, and
                             the band's own end is inverted.  g = 1 at 0 dB,
                             where S is a first-order ALLPASS.)
      cut,   allpass_flat:   S = A * 1/(1 + (G-1)*F),  A = 1 - 2*F, the same
                             allpass, so the two branches meet at 0 dB.
      boost, not allpass:    S = 1 + (G-1)*F        (minimum phase)
      cut,   not allpass:    S = 1/(1 + (G-1)*F)

    Which of the two families a band uses was decided by fitting the MEASURED
    COMPLEX ratio-to-flat, magnitude and phase together, with one delay and
    one broadband gain fitted out: Bass and Treble boosts fit the 1-2g*F form
    to 0.00-0.12 deg rms and the minimum-phase form to 7-57 deg; Bass, Treble
    and Mid cuts and Mid boosts fit the minimum-phase form to 0.01-0.33 deg.
    """
    n = len(b)
    # at 0 dB the section is exactly the flat state (the allpass, or unity):
    # the measured gain offsets are a boost/cut calibration, not a DC term.
    G = 1.0 if gdb == 0 else 10.0 ** ((abs(gdb) + (off_boost if gdb > 0 else off_cut)) / 20.0)
    if gdb > 0 and allpass_flat:
        g = (1.0 + G) / 2.0
        num = [a[i] - 2.0 * g * b[i] for i in range(n)]
        den = list(a)
    elif gdb > 0:
        num = [a[i] + (G - 1.0) * b[i] for i in range(n)]
        den = list(a)
    else:
        k = G - 1.0
        # A/(1 + k*F) with A = (a - 2b)/a and 1/(1+kF) = a/(a + k*b): the two
        # a's cancel exactly, so the cut stays the same order as the boost.
        num = [a[i] - 2.0 * b[i] for i in range(n)] if allpass_flat else list(a)
        den = [a[i] + k * b[i] for i in range(n)]
    g0 = den[0]
    return [v / g0 for v in num], [v / g0 for v in den]


MID_FREQ_STEPS_HZ = [250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
                     2500, 3150, 4000]


def eq3_snap_mid_freq(hz, steps=None):
    """The unit's Mid Freq is a 13-entry list (ISO third-octave nominals, read
    off the unit by Mark 2026-09-17, AX30G_HANDOFF.md Sec 1); snap to the
    nearest of them in LOG frequency, which is how the steps are spaced."""
    st = steps or MID_FREQ_STEPS_HZ
    return min(st, key=lambda v: abs(math.log(max(hz, 1e-6)) - math.log(v)))


def eq3_sections(spec, fs):
    """The three band sections (and the Trim gain) for the 3BEQ block, as a
    list of (name, b, a) plus the linear Trim gain.  Exposed so the tests and
    the C++ spec can quote exactly the coefficients the renderer runs."""
    p = spec["params"]
    m = spec.get("maps", {})
    bass = float(p.get("Bass", 0.0))
    mid_g = float(p.get("Mid Gain", 0.0))
    mid_f = float(eq3_snap_mid_freq(float(p.get("Mid Freq", 1000.0)),
                                    m.get("mid", {}).get("steps_hz") or spec.get("mid_freq_steps_hz")))
    treb = float(p.get("Treble", 0.0))
    trim = float(p.get("Trim Gain", p.get("Trim", 0.0)))

    out = []
    cb = m.get("bass", {})
    ap_b = bool(cb.get("allpass_flat", True))
    if bass != 0.0 or ap_b:
        b, a = _eq3_lp1(fs, cb["corner_hz"])
        out.append(("bass",) + _eq3_section(b, a, bass, cb.get("gain_offset_boost_db", 0.0),
                                            cb.get("gain_offset_cut_db", 0.0), ap_b))
    cm = m.get("mid", {})
    ap_m = bool(cm.get("allpass_flat", False))
    if mid_g != 0.0 or ap_m:
        q = cm["q_boost"] if mid_g > 0 else cm["q_cut"]
        b, a = _eq3_bp2(fs, mid_f, q, cm.get("alpha_law", "w0"))
        out.append(("mid",) + _eq3_section(b, a, mid_g, cm.get("gain_offset_boost_db", 0.0),
                                           cm.get("gain_offset_cut_db", 0.0), ap_m))
    ct = m.get("treble", {})
    ap_t = bool(ct.get("allpass_flat", True))
    if treb != 0.0 or ap_t:
        b, a = _eq3_hp1(fs, ct["corner_hz"])
        out.append(("treble",) + _eq3_section(b, a, treb, ct.get("gain_offset_boost_db", 0.0),
                                              ct.get("gain_offset_cut_db", 0.0), ap_t))
    trim_lin = 10.0 ** (trim / 20.0)
    return out, trim_lin


def render_3beq(x, spec, fs):
    """3-Band EQ (3BEQ), Block 1's last slot, measured 2026-09-17.

    Signal order, which is what the captures decide (docs/3beq-model-2026-09-17.md):

        x -> Trim Gain (a plain scalar) -> Bass -> Mid -> Treble -> hard clip

    Every band is "1 + k*F" for a boost and its exact reciprocal
    "1/(1 + k*F)" for a cut, k = 10^(|dB|/20) - 1, with F a FIXED filter:
    a bilinear one-pole low-pass at 79.9 Hz (Bass), a bilinear one-pole
    high-pass at 8.0 kHz (Treble), and a constant-peak-gain band-pass at
    the displayed Mid Freq step (Mid).  Nothing about F moves with the gain
    setting; the whole gain law is the one scalar k.

    The block runs in the PRE-EMPHASISED domain -- the de-emphasis that
    matches the input stage's analog pre-emphasis sits after the DSP, not
    before it -- so the clip ceiling is reached first at high frequencies.
    The spec turns that on with "input_stage": {..., "de_emphasis":
    "after_effect"}; engine/render.py does the actual deferring.

    blocks.clip.position: "output" (default; one clip after the three bands),
    "per_band" (clip after every section) or "none".
    blocks.clip.level: the ceiling as a fraction of converter full scale.

    blocks.path: the block's FIXED path, measured as (flat 3BEQ capture) /
    (Bypass capture) -- a gain, an integer delay in device samples, and a
    first-order allpass. The allpass corner lands on the Bass band's own
    fixed corner, so at Bass 0 the Bass section is not bypassed: it runs as
    an allpass. This is invisible to every response measurement in
    docs/3beq-response-*.md (they all divide by the flat row) and is only
    here so a time-domain null against a real capture can work.
    """
    from scipy.signal import lfilter
    b = spec.get("blocks", {})
    clip = b.get("clip", {})
    pos = clip.get("position", "output")
    lvl = float(clip.get("level", 1.0))
    sections, trim = eq3_sections(spec, fs)
    xin = (x[:, 0] + x[:, 1]) * 0.5
    y = xin * trim
    if pos == "per_band" and lvl > 0:
        y = np.clip(y, -lvl, lvl)
    for name, bb, aa in sections:
        y = lfilter(bb, aa, y)
        if pos == "per_band" and lvl > 0:
            y = np.clip(y, -lvl, lvl)
    if pos == "output" and lvl > 0:
        y = np.clip(y, -lvl, lvl)
    path = b.get("path") or {}
    if path.get("allpass_hz"):
        T = math.tan(math.pi * float(path["allpass_hz"]) / fs)
        c = (1.0 - T) / (1.0 + T)
        y = lfilter([c, -1.0], [1.0, -c], y)
    nd = int(path.get("delay_samples", 0))
    if nd:
        y = np.concatenate([np.zeros(nd), y])[: len(y)]
    if path.get("gain_db"):
        y = y * 10.0 ** (float(path["gain_db"]) / 20.0)
    out = np.zeros_like(x)
    out[:, 0] = y
    out[:, 1] = y
    truth = {
        "effect": "3BEQ", "trim_gain": trim,
        "sections": [{"band": n, "b": list(bb), "a": list(aa)} for n, bb, aa in sections],
        "clip_position": pos, "clip_level": lvl,
    }
    return out, truth


def _rev_type_table(spec):
    """The per-Type structure table of a REV spec."""
    t = str(spec["params"].get("Type", "HALL")).upper()
    types = spec["types"]
    if t not in types:
        raise KeyError(f"REV type {t} not in the model ({sorted(types)})")
    return t, types[t]


def _rev_comb(x, d, g, a):
    """One recirculating comb, optionally with a first-order lowpass of pole
    `a` in the feedback path:

        w[n] = (1-a)*y[n-d] + a*w[n-1];   y[n] = x[n] + g*w[n]

    Evaluated a block of `d` samples at a time: everything block k feeds back
    from lies entirely in block k-1, so the recursion is one vector operation
    per block and the whole comb costs O(N), not O(N*d) as `lfilter` with a
    length-d denominator would.
    """
    from scipy.signal import lfilter
    n = len(x)
    y = np.array(x, dtype=float, copy=True)
    if g <= 0.0:
        return y
    zi = np.zeros(1)
    lp_b, lp_a = [1.0 - a], [1.0, -a]
    prev = np.zeros(d)
    for s0 in range(0, n, d):
        e0 = min(s0 + d, n)
        k = e0 - s0
        fb = prev[:k]
        if a > 0.0:
            fb, zi = lfilter(lp_b, lp_a, fb, zi=zi)
        y[s0:e0] += g * fb
        prev = np.zeros(d)
        prev[:k] = y[s0:e0]
    return y


def _rev_allpass(x, L, u):
    """Schroeder allpass with a negative coefficient,
    y[n] = u*v[n] + v[n-L] - u*y[n-L], evaluated L samples at a time (same
    reason as _rev_comb)."""
    n = len(x)
    y = np.empty(n)
    prev_in = np.zeros(L)
    prev_out = np.zeros(L)
    for s0 in range(0, n, L):
        e0 = min(s0 + L, n)
        k = e0 - s0
        blk = u * x[s0:e0] + prev_in[:k] - u * prev_out[:k]
        y[s0:e0] = blk
        pi = np.zeros(L); pi[:k] = x[s0:e0]; prev_in = pi
        po = np.zeros(L); po[:k] = blk; prev_out = po
    return y


def _shift(x, k, n):
    """x delayed by k samples, length n."""
    out = np.zeros(n)
    if k < n:
        m = min(n - k, len(x))
        out[k:k + m] = x[:m]
    return out


def _rev_poles(spec, T, fs):
    """The four in-loop High Damp lowpass poles for the current settings.

    `high_damp_scaling: "rt_length"` (docs/rev-highdamp-2026-09-18.md, the
    2026-09-18 High Damp law, `models/ax30g-rev-hd-2026-09-18.json`):
    `maps.high_damp` gives A(HighDamp), the pole of the comb with the
    LONGEST rt_length, and every other comb's pole is A scaled by that
    comb's own rt_length. Anything else (a pass-2-shaped spec with a
    per-Type `high_damp_map`, or no `high_damp_scaling` at all) falls back
    to the old single per-Type pole, so an old spec still renders exactly
    as it always did.
    """
    hd = float(spec["params"]["High Damp"])
    rtl = np.array(T.get("rt_length", T["combs"]), dtype=float)
    scaling = T.get("high_damp_scaling", spec.get("high_damp_scaling"))
    if scaling == "rt_length":
        A = float(apply_map(spec["maps"]["high_damp"], hd, fs))
        A = min(max(A, 0.0), float(spec.get("high_damp_max_pole", 0.995)))
        return A * rtl / rtl.max()
    a = float(apply_map(T.get("high_damp_map", spec["maps"]["high_damp"]), hd, fs))
    return np.full(4, a)


def render_rev(x, spec, fs):
    """Reverb: four parallel feedback combs into a per-channel series
    Schroeder allpass cascade (docs/rev-model-2026-09-18.md,
    docs/rev-highdamp-2026-09-18.md).

        x -> predelay -> input_delay[ch] -> +-- comb(d1,g1) --> read[1][ch] --+
                                            +-- comb(d2,g2) --> read[2][ch] --+
                                            +-- comb(d3,g3) --> read[3][ch] --+--> sum*mix
                                            +-- comb(d4,g4) --> read[4][ch] --+
                                                                              |
              wet[ch] = -input_gain * AP(L1,u1) AP(L2,u2) AP(L3,u3) <---------+

    The four comb delays are shared by the two channels; each channel reads
    each comb at its own offset, and each channel has its own allpass
    lengths (two of the three are shared with the other channel and share
    that section's coefficient).

    Rev Time sets every comb's gain from its own length:
    g_i = 10^(-3*rt_length_i/(RevTime*fs)), i.e. each comb's RT60 is the
    displayed Rev Time. High Damp puts a one-pole lowpass, unity at DC, in
    each comb's feedback path; on a `high_damp_scaling: "rt_length"` spec
    the pole is per comb (`_rev_poles`), scaled by that comb's own
    `rt_length` against the Type's longest -- the measured 2026-09-18 law.
    Balance crossfades dry against wet with two measured tables.
    """
    p = spec["params"]
    m = spec["maps"]
    tname, T = _rev_type_table(spec)
    n = x.shape[0]
    mono = 0.5 * (x[:, 0] + x[:, 1]) if spec.get("input", "sum") == "sum" else x[:, 0]

    pre = int(round(apply_map(m["pre_delay"], p["Pre Dly"], fs)))
    poles = _rev_poles(spec, T, fs)
    wet = float(apply_map(m["balance_wet"], p["Balance"], fs))
    dry = float(apply_map(m["balance_dry"], p["Balance"], fs))
    pol = float(T.get("wet_polarity", spec.get("wet_polarity", -1.0)))
    ing = float(T.get("input_gain", 1.0))
    mix = float(T.get("comb_mix", spec.get("comb_mix", 0.5)))

    combs = [int(v) for v in T["combs"]]
    rtlen = [int(v) for v in T.get("rt_length", combs)]
    reads = [[int(a), int(b)] for a, b in T["reads"]]
    ind = [int(v) for v in T.get("input_delay", [0, 0])]

    rt = max(float(p["Rev Time"]), 1e-6)
    gs = [10.0 ** (-3.0 * D / (rt * fs)) for D in rtlen]
    gs = [min(g, 0.9999) for g in gs]

    # tail room: the network keeps ringing after the input stops, and the
    # host-rate render is trimmed to n anyway, so pad by the longest path
    pad = pre + max(ind) + max(max(r) for r in reads) + max(combs)
    N = n + pad + int(0.2 * fs)
    src = _shift(mono, pre, N)

    outs = []
    combouts = [_rev_comb(src, d, g, float(a)) for d, g, a in zip(combs, gs, poles)]
    for ch in (0, 1):
        v = np.zeros(N)
        for co, r in zip(combouts, reads):
            v += mix * _shift(co, ind[ch] + r[ch], N)
        aps = T["allpass_l"] if ch == 0 else T["allpass_r"]
        for L, u in aps:
            v = _rev_allpass(v, int(L), float(u))
        outs.append(pol * ing * v[:n])

    y = np.stack([dry * mono + wet * outs[0], dry * mono + wet * outs[1]], axis=1)
    truth = {"effect": "REV", "type": tname, "pre_delay_samples": pre,
             "combs": combs, "comb_gains": gs, "reads": reads,
             "input_delay": ind, "comb_mix": mix,
             "damp_poles": [float(v) for v in poles],
             "allpass_l": T["allpass_l"], "allpass_r": T["allpass_r"],
             "wet": wet, "dry": dry, "input_gain": ing}
    return y, truth


RENDERERS = {"SDLY": render_sdly, "MODD": render_modd, "SMOD": render_smod, "REV": render_rev,
             "CHO": render_cho, "SCHO": render_scho,
             "3BEQ": render_3beq}

# COMP lives in its own module (engine/comp.py) because this file was being
# edited by another session on 2026-09-18, when the compressor was measured.
# The import is at the bottom on purpose: engine/comp.py imports RENDERERS
# from here, and RENDERERS is already bound by the time this line runs.
from .comp import render_comp                                   # noqa: E402
RENDERERS["COMP"] = render_comp
from .hypr import render_hypr                                   # noqa: E402
RENDERERS["HYPR"] = render_hypr
