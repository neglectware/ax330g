import json
import os
import numpy as np
import soundfile as sf
from fractions import Fraction
from scipy.signal import resample_poly, firwin, sosfilt, fftconvolve, lfilter
from scipy.optimize import least_squares
from .effects import RENDERERS
from .blocks import quantize


def load_spec(path):
    with open(path) as f:
        return json.load(f)


def _ratio(fs_from, fs_to):
    fr = Fraction(fs_to / fs_from).limit_denominator(4096)
    return fr.numerator, fr.denominator


def _converter(x, bits):
    if not bits:
        return x
    scale = float(1 << (bits - 1))
    return np.clip(np.round(x * scale), -scale, scale - 1) / scale


def load_chain(spec, spec_path=None):
    """spec["chain"]: inline dict, or a file name relative to the spec's
    directory (models/) holding the measured converter chain
    (see models/ax30g-chain.json). Returns the dict or None."""
    c = spec.get("chain")
    if c is None:
        return None
    if isinstance(c, str):
        base = os.path.dirname(spec_path) if spec_path else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "models")
        with open(os.path.join(base, c)) as f:
            return json.load(f)
    return c


def load_input_stage(spec, spec_path=None):
    """spec["input_stage"]: inline dict, or a file name relative to the spec's
    directory (models/) holding the measured analog input stage
    (see models/ax30g-input-stage.json). Returns the dict or None.

    The stage is OFF unless a spec names it: no existing model carries the
    key, so every null test and render made before 2026-09-16 is unchanged.

    A dict carrying a "file" key is the file's contents with the dict's own
    other keys written over them, so a model can borrow the measured stage
    and change one thing (the 3BEQ model borrows it and sets
    "de_emphasis": "after_effect")."""
    c = spec.get("input_stage")
    if c is None:
        return None

    def _read(name):
        base = os.path.dirname(spec_path) if spec_path else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "models")
        with open(os.path.join(base, name)) as f:
            return json.load(f)

    if isinstance(c, str):
        return _read(c)
    if isinstance(c, dict) and "file" in c:
        st = _read(c["file"])
        st.update({k: v for k, v in c.items() if k != "file"})
        return st
    return c


def shelf1_coeffs(fs, tau_zero_s, tau_pole_s, f_lo=20.0, n=400):
    """First-order digital filter matching the analog shelf
    H(s) = (1 + s*tau_zero)/(1 + s*tau_pole), unity at DC.

    A bilinear transform with each corner prewarped is 2.2 dB high at 17 kHz
    at the device rate (the two corners cannot both be prewarped in one
    bilinear), so the digital zero and pole are fitted to the analog
    magnitude over f_lo..0.999*Nyquist instead: 0.21 dB max / 0.03 dB rms
    error for the measured 63.94/18.17 us (2026-09-16). Returns (b, a),
    b = [K, -K*rz], a = [1, -rp]; the inverse filter is (a, b) exactly."""
    fmax = fs / 2 * 0.999
    f = np.geomspace(f_lo, fmax, n)
    target = 10 * np.log10((1 + (2 * np.pi * f * tau_zero_s) ** 2) / (1 + (2 * np.pi * f * tau_pole_s) ** 2))
    z = np.exp(-2j * np.pi * f / fs)

    def mag(p):
        rz, rp = p
        K = (1 - rp) / (1 - rz)
        return 20 * np.log10(np.abs(K * (1 - rz * z) / (1 - rp * z)))

    sol = least_squares(lambda p: mag(p) - target, [-0.5, -0.2],
                        bounds=([-0.999, -0.999], [0.999, 0.999]))
    rz, rp = sol.x
    K = (1 - rp) / (1 - rz)
    return np.array([K, -K * rz]), np.array([1.0, -rp])


def apply_input_stage(x, fs, st, bits=None, defer_de_emphasis=False):
    """The analog input stage, applied at the device rate just before the
    converter's quantize: Input Level, the +15 dB amplifier's pre-emphasis
    shelf, the ADC's full-scale ceiling, then the de-emphasis that makes the
    measured through-response flat.

    Below the ceiling the shelf and its exact inverse cancel, so the stage is
    transparent and a LIN render is unchanged. Returns (y, info) where info
    carries the Peak-LED state. Measured 2026-09-16, see
    docs/input-stage-2026-09-16.md.

    `defer_de_emphasis`: return before the de-emphasis filter and hand its
    coefficients back in info["de_emphasis_ba"], so the caller can run the
    effect INSIDE the pre-emphasised domain and de-emphasise afterwards. The
    3BEQ block needs this: its clip ceiling is reached first at high
    frequencies, which is only true if the EQ sees the pre-emphasised signal
    (docs/3beq-model-2026-09-17.md). Spec key: input_stage.de_emphasis =
    "after_effect".

    `bits`: quantize between the clip and the de-emphasis, which is where the
    converter actually sits. It matters because the de-emphasis overshoots
    full scale by up to 0.22 dB on a clipped ramp (peak 1.026, 16,538 samples
    over), so leaving render_spec's own _converter to run after the stage
    saturates those a second time at a point where the hardware has a wider
    word. Worth 0.1 dB of ramp null either way (measured 2026-09-16); it is
    here because it is the right place, not because it bought anything."""
    info = {}
    if not st or not st.get("enabled", True):
        return x, info
    c = st.get("ceiling") or {}
    # The ADC's full scale IS the DSP's full scale, so the ceiling is +-1 here
    # and the calibration lives in the gain: headroom_dbfs is how far the input
    # file's 0 dBFS sits BELOW that ceiling with Input Level at LIN (+2.070 dB
    # measured, i.e. a full-scale 1 kHz sine at LIN still cannot reach it).
    hs = float(c.get("headroom_dbfs", 0.0))
    g = 10.0 ** ((float(st.get("input_level_db", 0.0)) + float(st.get("gain_trim_db", 0.0)) - hs) / 20.0)
    info["gain_db"] = 20 * np.log10(g)
    y = x * g
    pe = st.get("pre_emphasis") or {}
    b = a = None
    if pe.get("tau_zero_us"):
        b, a = shelf1_coeffs(fs, pe["tau_zero_us"] * 1e-6, pe["tau_pole_us"] * 1e-6)
        y = lfilter(b, a, y, axis=0)
    off = float(c.get("offset_frac", 0.0))
    peak = float(np.max(np.abs(y))) if y.size else 0.0
    info["clipper_peak_dbfs"] = 20 * np.log10(max(peak, 1e-12))
    led = float(st.get("peak_led", {}).get("margin_db", -1.0))
    info["peak_led"] = bool(peak >= 10.0 ** (led / 20.0))
    info["clipped_samples"] = int(np.sum(np.abs(y + off) > 1.0))
    if c.get("type", "hard_clip") == "hard_clip":
        os_n = int(c.get("oversample", 1))
        if os_n > 1:
            # the real clip is analog, ahead of the converter's decimation
            # filter, so its corners are not on the sample grid; clipping at
            # os_n times the device rate and band-limiting back models that.
            # Measured 2026-09-16: it makes the ramp null WORSE, because the
            # capture keeps the fold-back of the harmonics above Nyquist that
            # the decimation removes here. Kept as an option, default 1.
            up = resample_poly(y, os_n, 1, axis=0)
            up = np.clip(up + off, -1.0, 1.0) - off
            y = resample_poly(up, 1, os_n, axis=0)[: y.shape[0]]
        else:
            y = np.clip(y + off, -1.0, 1.0) - off
    if bits:
        y = _converter(y, bits)
    if b is not None:
        if defer_de_emphasis:
            info["de_emphasis_ba"] = (np.asarray(a), np.asarray(b))
        else:
            y = lfilter(a, b, y, axis=0)
    return y, info


def _hpf1_sos(fs, fc):
    """First-order high-pass s/(s+wc), bilinear with the corner prewarped."""
    K = 2 * fs * np.tan(np.pi * fc / fs)
    c = 2 * fs
    a0 = c + K
    return [c / a0, -c / a0, 0.0, 1.0, (K - c) / a0, 0.0]


def chain_fir(chain, fs, pre_s=0.002, post_s=0.005, nfft=32768):
    """The chain's HF response as a FIR at rate fs. Returns (h, pre_samples):
    t=0 of the response sits at index pre_samples."""
    hf = chain["hf"]
    fgrid = np.arange(len(hf["re"])) * float(hf["f_step_hz"])
    f = np.fft.rfftfreq(nfft, 1.0 / fs)
    H = np.interp(f, fgrid, hf["re"], right=0.0) + 1j * np.interp(f, fgrid, hf["im"], right=0.0)
    h = np.fft.irfft(H, nfft)
    pre, post = int(round(pre_s * fs)), int(round(post_s * fs))
    hh = np.concatenate([h[-pre:], h[:post]])
    e = int(0.001 * fs)
    r = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, e))
    hh[:e] *= r
    e2 = int(0.0016 * fs)
    r2 = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, e2))
    hh[-e2:] *= r2[::-1]
    return hh, pre


def apply_chain(y, fs, chain):
    """Unit converter chain at the I/O rate: LF high-pass cascade, then the
    measured HF response as a short FIR (pre-delay compensated, so the
    chain adds no latency of its own)."""
    if not chain:
        return y
    out = y
    hz = chain.get("lf_hpf_hz") or []
    if hz:
        sos = np.array([_hpf1_sos(fs, float(v)) for v in hz])
        out = sosfilt(sos, out, axis=0)
    if chain.get("hf"):
        h, pre = chain_fir(chain, fs)
        out = np.stack([fftconvolve(out[:, c], h, mode="full")[pre: pre + out.shape[0]] for c in range(out.shape[1])], axis=1)
    return out


def resampler_window(spec, up, down):
    """resampler.half_mult (default 10, scipy's own) sets the low-pass length
    to half_mult*max(up,down) taps per side. 10 rolls off from ~12 kHz and is
    -9 dB at 19 kHz round trip; 100 is flat to 19 kHz (-1.4 dB at 19.2 k),
    which is what the measured chain needs in front of it (2026-09-14)."""
    rs = spec.get("resampler", {})
    if "window" in rs:
        return rs["window"]
    mult = int(rs.get("half_mult", 10))
    if mult == 10:
        return ("kaiser", 14.0)
    mx = max(up, down)
    return firwin(2 * mult * mx + 1, 1.0 / mx, window=("kaiser", 14.0))


def render_spec(spec, x, fs_in, spec_path=None):
    """x: (N,) or (N,2) float at fs_in. Returns (y (N,2) at fs_in, truth)."""
    fs = float(spec.get("sample_rate", 39062.5))
    if x.ndim == 1:
        x = np.stack([x, x], axis=1)
    up, down = _ratio(fs_in, fs)
    # a sharp anti-alias / reconstruction filter: the unit's converters pass
    # to ~19 kHz and the measured chain EQ carries their true roll-off, so
    # the model's own resampling must not add an earlier one
    win = resampler_window(spec, up, down)
    bits = spec.get("converters", {}).get("bits", 18)
    stage = load_input_stage(spec, spec_path)
    stage_info = {}
    if stage and stage.get("rate") == "input":
        # the shelf and the ceiling ahead of the band-limiting, i.e. treating
        # the host rate as the analog domain. Measured 2026-09-16: it halves
        # the model's over-clipping of a single-sample click but removes the
        # fold-back of the clip harmonics above the device Nyquist, which the
        # capture has. Default is "device".
        x, stage_info = apply_input_stage(x, fs_in, stage)
        xd = resample_poly(x, up, down, axis=0, window=win)
        xd = _converter(xd, bits)
    else:
        xd = resample_poly(x, up, down, axis=0, window=win)
        defer = bool(stage and stage.get("de_emphasis") == "after_effect")
        xd, stage_info = apply_input_stage(xd, fs, stage, bits, defer_de_emphasis=defer)
        if not stage_info:
            xd = _converter(xd, bits)
    # Ducking (SDLY/XDLY/TDLY/HDLY): a gain on the wet output driven by the
    # input envelope, measured 2026-09-18. Imported lazily so engine/ducking.py
    # can import from here. Ducking 0, or a spec with no "ducking" key, takes
    # the ordinary path and is bit-identical to it.
    from .ducking import DUCKED_EFFECTS, load_ducking, render_sdly_ducking
    duck = (load_ducking(spec, spec_path)
            if spec["effect"] in DUCKED_EFFECTS and float(spec["params"].get("Ducking", 0))
            else None)
    if duck:
        y, truth = render_sdly_ducking(xd, dict(spec, ducking=duck), fs)
    else:
        y, truth = RENDERERS[spec["effect"]](xd, spec, fs)
    de = stage_info.get("de_emphasis_ba") if stage_info else None
    if de is not None:
        # the effect ran in the pre-emphasised domain (see apply_input_stage)
        y = lfilter(de[0], de[1], y, axis=0)
    y = _converter(y, bits)
    yo = resample_poly(y, down, up, axis=0, window=win)
    yo = yo[: x.shape[0]]
    yo = apply_chain(yo, fs_in, load_chain(spec, spec_path))
    truth["sample_rate"] = fs
    if stage_info:
        truth["input_stage"] = stage_info
    return yo, truth


def render_file(spec_path, in_wav, out_wav):
    spec = load_spec(spec_path)
    x, fs = sf.read(in_wav, dtype="float64", always_2d=True)
    if x.shape[1] == 1:
        x = x[:, 0]
    y, truth = render_spec(spec, x, fs, spec_path=spec_path)
    sf.write(out_wav, y.astype(np.float32), int(fs), subtype="FLOAT")
    return truth
