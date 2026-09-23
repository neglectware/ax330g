"""Plots one PNG per capture with the panels a human (or Claude) reads."""
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from .util import cut, seg, db


def plot_capture(name, x, fs, layout, clicks_res, storage_res=None, lfo_res=None, out_png=None, truth=None):
    fig, axes = plt.subplots(2, 3, figsize=(16, 8))
    fig.suptitle(name, fontsize=12)
    ch = clicks_res.get("channel", 0)
    # 1: first click response, log amplitude
    ax = axes[0, 0]
    s = seg(layout, "clicks")
    tc = s["click_times"][0]
    w = cut(x[:, ch], fs, tc - 0.01, tc + 1.3)
    t = np.arange(len(w)) / fs * 1000 - 10
    ax.plot(t, db(np.abs(w) + 1e-6), lw=0.4)
    for c in clicks_res["clicks"][:1]:
        for tr, a in c.get("peaks", []):
            ax.axvline(tr, color="r", alpha=0.3, lw=0.8)
    ax.set_ylim(-100, 0)
    ax.set_xlabel("ms after click")
    ax.set_ylabel("dBFS")
    ax.set_title(f"click 1 response; delay {clicks_res.get('delay_ms_median', float('nan')):.2f} ms")
    # 2: loop response G2 = R2/R1 with the one-pole fit
    ax = axes[0, 1]
    if "g2_mean" in clicks_res:
        f = np.array(clicks_res["bands_hz"])
        g2 = np.array(clicks_res["g2_mean"])
        ax.semilogx(f, db(g2), label="R2/R1 (loop)")
        if "g1_mean" in clicks_res:
            g1 = np.array(clicks_res["g1_mean"])
            ax.semilogx(f, db(g1), label="R1/dry", alpha=0.6)
        fb = clicks_res.get("fb_coef")
        fc = clicks_res.get("damp_hz")
        if fb:
            from .clicks import onepole_mag
            ax.semilogx(f, db(fb * onepole_mag(f, fc)), "k--", lw=0.8,
                        label=f"fit fb={fb:.3f} fc={fc:.0f} Hz" if fc else f"fit fb={fb:.3f}, no damp")
        ax.set_ylim(-40, 5)
        ax.legend(fontsize=8)
    ax.set_title("repeat spectra ratios")
    ax.set_xlabel("Hz")
    ax.grid(True, which="both", alpha=0.3)
    # 3: repeat offsets (for MODD) or amplitude ratios
    ax = axes[0, 2]
    for c in clicks_res["clicks"]:
        pk = c.get("peaks", [])
        if len(pk) > 1:
            ax.plot([p[0] for p in pk], db([p[1] for p in pk]), "o-", ms=3, lw=0.8)
    ax.set_xlabel("ms")
    ax.set_ylabel("peak dBFS")
    ax.set_title("repeat peaks per click")
    ax.grid(True, alpha=0.3)
    # 4: storage: noise repeat/dry spectrum ratio
    ax = axes[1, 0]
    if storage_res and "noise_bands_hz" in storage_res:
        f = np.array(storage_res["noise_bands_hz"])
        ax.semilogx(f, storage_res["noise_ratio_db"])
        for k in ("bw_3db_hz", "bw_20db_hz"):
            v = storage_res.get(k)
            if v:
                ax.axvline(v, color="r", alpha=0.4, lw=0.8)
        ax.set_ylim(-50, 10)
        s20 = storage_res.get("sine20", {})
        s40 = storage_res.get("sine40", {})
        ax.set_title(f"noise repeat/dry; SINAD wet -20: {s20.get('wet_sinad_db', float('nan')):.1f} dB, "
                     f"-40: {s40.get('wet_sinad_db', float('nan')):.1f} dB")
    ax.set_xlabel("Hz")
    ax.grid(True, which="both", alpha=0.3)
    # 5: wet-only sine tail spectrum (quantization texture)
    ax = axes[1, 1]
    if storage_res and "delay_ms_used" in storage_res:
        s = seg(layout, "sine20")
        from .storage import wet_tail
        tail = wet_tail(x[:, ch], fs, s, storage_res["delay_ms_used"])
        if tail is not None:
            n = len(tail)
            X = db(np.abs(np.fft.rfft(tail * np.hanning(n))) / n * 2)
            fr = np.fft.rfftfreq(n, 1.0 / fs)
            ax.plot(fr, X, lw=0.4)
            for fi, lv in storage_res.get("image_peaks", [])[:6]:
                ax.annotate(f"{fi / 1000:.2f}k {lv:.0f}dB", (fi, X[int(round(fi * n / fs))]), fontsize=7, color="r")
            ax.set_ylim(-140, 0)
            ax.set_xlim(0, fs / 2)
            ax.set_title(f"wet-only sine tail; rate_div verdict /{storage_res.get('rate_div', '?')}, "
                         f"floor drop {storage_res.get('floor_drop_db', float('nan')):.1f} dB -> "
                         f"{storage_res.get('companding_verdict', '?')}")
    ax.set_xlabel("Hz")
    ax.grid(True, alpha=0.3)
    # 6: pitch deviation
    ax = axes[1, 2]
    if lfo_res and "dev" in lfo_res:
        ax.plot(lfo_res["dev_t"], np.array(lfo_res["dev"]) * 100, lw=0.6)
        ax2 = ax.twinx()
        ax2.plot(lfo_res["dev_t"], lfo_res["d_ms"], color="orange", lw=0.6, alpha=0.7)
        ax2.set_ylabel("delay mod (ms)")
        ax.set_ylabel("freq deviation (%)")
        ax.set_title(f"LFO {lfo_res['lfo_hz']:.3f} Hz, depth {lfo_res['depth_ms']:.2f} ms, "
                     f"{lfo_res['shape']} (crest {lfo_res['crest']:.2f}), step {lfo_res['step_ratio']:.2f}")
    ax.set_xlabel("s")
    ax.grid(True, alpha=0.3)
    if truth:
        fig.text(0.01, 0.01, "truth: " + _truth_line(truth), fontsize=8, family="monospace")
    fig.tight_layout(rect=(0, 0.03, 1, 0.97))
    if out_png:
        fig.savefig(out_png, dpi=110)
    plt.close(fig)


def _truth_line(t):
    if t.get("effect") == "SDLY":
        c = t["channels"][0]
        st = c["storage"]
        return (f"delay {c['delay_ms']:.2f} ms fb {c['fb_coef']:.3f} damp {c['damp_hz']} wet {c['wet']} "
                f"storage {st['bits']}b {st['compand']} /{st['rate_div']} {st['prefilter']}/{st['upsample']}")
    if t.get("effect") == "MODD":
        return (f"delay {t['delay_ms']:.2f} ms fb {t['fb_coef']:.3f} damp {t['damp_hz']} lfo {t['lfo_hz']} Hz "
                f"{t['lfo_shape']} upd {t['lfo_update_every']} depth {t['depth_ms']} ms in_loop {t['in_loop']}")
    return str(t)
