"""HYPR (Hyper Resonator) analysis: what the bench wants to see after a
Block-1 Hyper Resonator capture.

Two pictures and the numbers behind them:

* the **transfer curve** -- the 1 kHz component of the output through the
  5 s ramp against the input level, i.e. the block's describing function.
  That is what the driver is, and it is the only view in the signal set
  where a static nonlinearity can be read directly.
* a **spectrogram** of the DI clip with the resonator's peak-frequency
  track drawn on it, plus the input's peak envelope, which is what the
  sweep is.

Plus, in the JSON: the steady-state harmonic table of the -20 dBFS sine,
the resting resonance from the click ring, and the envelope-to-corner
points binned out of the DI clip.

Everything is measured against `captures/AX30G_BYPASS_IN-LIN.wav`, never
against the grid's own first row: at Type 1 / Harmonics 0 the effect path
is 50 dB down and is not a usable reference for anything.
"""
import os
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_BYPASS = os.path.join(ROOT, "captures", "AX30G_BYPASS_IN-LIN.wav")
DEFAULT_SIGNAL = os.path.join(ROOT, "capture", "signalset-normal.wav")
DEFAULT_LAYOUT = os.path.join(ROOT, "capture", "layout-normal.json")


def peak_envelope(x, fs, tau_a_ms=1.2, tau_r_ms=200.0):
    import math
    aa = 1.0 - math.exp(-1.0 / (tau_a_ms * 1e-3 * fs))
    ar = 1.0 - math.exp(-1.0 / (tau_r_ms * 1e-3 * fs))
    e = 0.0
    out = np.empty(len(x))
    ax = np.abs(x)
    for i in range(len(ax)):
        v = ax[i]
        e += (aa if v > e else ar) * (v - e)
        out[i] = e
    return out


def tone_curve(y, fs, layout, k=1):
    """(input dBFS, output dB at k kHz) through the ramp."""
    from scipy.signal import stft
    from .util import seg, cut
    rp = seg(layout, "ramp")
    c = cut(y, fs, rp["start"], rp["end"])
    f, t, Z = stft(c, fs, nperseg=4096, noverlap=4096 - 1024)
    i = int(round(1000 * k * 4096 / fs))
    a = np.max(np.abs(Z[i - 2:i + 3, :]), axis=0)
    lin = rp["dbfs_from"] + (rp["dbfs_to"] - rp["dbfs_from"]) * t / (rp["end"] - rp["start"])
    return lin, 20 * np.log10(np.maximum(a, 1e-12))


def harmonics(y, fs, t0=23.0, t1=25.0, nh=8):
    from .util import cut
    c = cut(y, fs, t0, t1)
    n = len(c)
    w = np.hanning(n)
    Y = np.abs(np.fft.rfft(c * w)) / (np.sum(w) / 2)
    out = []
    for k in range(1, nh + 1):
        i = int(round(1000 * k * n / fs))
        out.append(float(20 * np.log10(max(float(np.max(Y[i - 3:i + 4])), 1e-14))))
    return out


def peak_track(y, fs, t0, t1, fmin=150.0, fmax=14000.0, nper=1024, hop=256):
    from scipy.signal import stft
    from .util import cut
    c = cut(y, fs, t0, t1)
    f, t, Z = stft(c, fs, nperseg=nper, noverlap=nper - hop)
    A = np.abs(Z)
    m = (f >= fmin) & (f <= fmax)
    fr = f[m]
    A = A[m]
    ks = np.argmax(A, axis=0)
    return t + t0, fr[ks], 20 * np.log10(np.maximum(A[ks, np.arange(A.shape[1])], 1e-12))


def ring_frequency(y, fs, layout):
    """Resting resonance from the click ring: the spectrum of the 0.5 s after
    each click, peak between 150 and 900 Hz, median over the four clicks."""
    from .util import seg, cut
    out = []
    for t0 in seg(layout, "clicks")["click_times"]:
        c = cut(y, fs, t0 + 0.002, t0 + 0.5)
        n = len(c)
        X = np.abs(np.fft.rfft(c * np.hanning(n)))
        f = np.fft.rfftfreq(n, 1.0 / fs)
        m = (f > 150) & (f < 900)
        if m.any():
            out.append(float(f[m][int(np.argmax(X[m]))]))
    return float(np.median(out)) if out else None


def sweep_points(y, x_signal, fs, layout, nbins=3.0):
    """Corner-vs-envelope points binned out of the DI clip: the output
    spectrogram's peak frequency against the INPUT's peak envelope."""
    from .util import seg, cut
    di = seg(layout, "di")
    t, fq, a = peak_track(y, fs, di["start"], di["end"])
    env = peak_envelope(cut(x_signal, fs, di["start"], di["end"]), fs)
    idx = np.clip(((t - di["start"]) * fs).astype(int), 0, len(env) - 1)
    E = 20 * np.log10(np.maximum(env[idx], 1e-9))
    sel = a > (a.max() - 45)
    pts = []
    for lo in np.arange(-60, 0, nbins):
        m = sel & (E >= lo) & (E < lo + nbins)
        if m.sum() > 4:
            pts.append({"env_dbfs": float(lo + nbins / 2), "n": int(m.sum()),
                        "f_hz": float(np.median(fq[m]))})
    return pts


def analyze_one(capture, bypass, signal, layout_path, outdir=None):
    import soundfile as sf
    from .util import load_wav, load_layout, seg, cut
    from .run import load_aligned
    x, fs = load_wav(signal)
    x = x[:, 0]
    layout = load_layout(layout_path)
    cap, ai = load_aligned(capture, x, fs, layout)
    ref, _ = load_aligned(bypass, x, fs, layout)
    y = cap[:, 0]
    r = ref[:, 0]

    lin, oc = tone_curve(y, fs, layout)
    _, rc = tone_curve(r, fs, layout)
    res = {"align": ai,
           "send_offset_db": float(ai.get("ref_gain_db", 0.0)),
           "ring_hz": ring_frequency(y, fs, layout),
           "harmonics_sine20_dbfs": harmonics(y, fs),
           "harmonics_bypass_dbfs": harmonics(r, fs),
           "sweep_points": sweep_points(y, x, fs, layout),
           "curve": {"in_dbfs": [float(v) for v in lin[::4]],
                     "out_db": [float(v) for v in oc[::4]],
                     "bypass_db": [float(v) for v in rc[::4]]},
           "segment_rms_dbfs": {}}
    for s in layout["segments"]:
        c = cut(y, fs, s["start"], s["end"])
        res["segment_rms_dbfs"][s["name"]] = float(20 * np.log10(max(np.sqrt(np.mean(c ** 2)), 1e-12)))

    png = None
    if outdir:
        png = os.path.join(outdir, os.path.splitext(os.path.basename(capture))[0] + ".png")
        _plot(png, capture, y, x, fs, layout, lin, oc, rc, res)
    res["png"] = png
    return res


def _plot(png, capture, y, x_signal, fs, layout, lin, oc, rc, res):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from scipy.signal import stft
    from .util import seg, cut
    fig, ax = plt.subplots(2, 1, figsize=(11, 8))
    ax[0].plot(lin, oc, lw=1.2, label="HYPR, 1 kHz out")
    ax[0].plot(lin, rc, lw=1.0, ls="--", color="grey", label="Bypass")
    ax[0].set_xlabel("input, dBFS (1 kHz ramp)")
    ax[0].set_ylabel("output at 1 kHz, dB")
    ax[0].grid(alpha=0.3)
    ax[0].legend(loc="lower right", fontsize=8)
    ax[0].set_title(os.path.basename(capture), fontsize=9)

    di = seg(layout, "di")
    c = cut(y, fs, di["start"], di["end"])
    f, t, Z = stft(c, fs, nperseg=1024, noverlap=1024 - 256)
    S = 20 * np.log10(np.maximum(np.abs(Z), 1e-9))
    ax[1].pcolormesh(t, f, S, vmin=S.max() - 70, vmax=S.max(), shading="auto", cmap="magma")
    tt, fq, a = peak_track(y, fs, di["start"], di["end"])
    sel = a > (a.max() - 45)
    ax[1].plot(tt[sel] - di["start"], fq[sel], ".", ms=1.6, color="#66ddff", label="peak track")
    ax[1].set_yscale("log")
    ax[1].set_ylim(80, 16000)
    ax[1].set_xlabel("DI clip, s")
    ax[1].set_ylabel("Hz")
    ax[1].legend(loc="upper right", fontsize=8)
    fig.tight_layout()
    fig.savefig(png, dpi=110)
    plt.close(fig)
