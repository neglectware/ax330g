#!/usr/bin/env python3
"""COMP pass 2: extract everything the 11 COMP captures say about the
compressor, cache it, and fit candidate gain laws.

    analysis/comp2.py extract          # 11 captures -> out/comp2/*.npz
    analysis/comp2.py report           # read the cache, print the tables

Everything is a RATIO against captures/AX30G_BYPASS_IN-LIN.wav, so the
converter chain and the input stage cancel (same method as
analysis/comp_curve.py and analysis/eq_response.py).
"""
import os
import sys
import json
import numpy as np
from scipy.signal import butter, sosfiltfilt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)
from analysis.util import load_wav, load_layout, seg, cut, db          # noqa: E402
from analysis.run import load_aligned                                  # noqa: E402

SIGNAL = os.path.join(ROOT, "capture", "signalset-normal.wav")
LAYOUT = os.path.join(ROOT, "capture", "layout-normal.json")
BYPASS = os.path.join(ROOT, "captures", "AX30G_BYPASS_IN-LIN.wav")
CAPDIR = os.path.join(ROOT, "captures")
OUT = os.path.join(ROOT, "out", "comp2")


def comp_captures():
    return sorted(n for n in os.listdir(CAPDIR)
                  if n.startswith("AX30G_COMP_") and n.endswith(".wav"))


def params_from_name(n):
    import re
    out = {}
    for k, v in re.findall(r"([A-Za-z]+)-(-?\d+)", n.split("_IN-")[0]):
        if k in ("Sensitivity", "Level", "Attack"):
            out[k] = int(v)
    return out


def tone_env(x, fs, hz=1000.0, lp_hz=60.0, order=4):
    t = np.arange(len(x)) / fs
    z = x * np.exp(-2j * np.pi * hz * t)
    sos = butter(order, lp_hz, "low", fs=fs, output="sos")
    return 2.0 * np.hypot(sosfiltfilt(sos, z.real), sosfiltfilt(sos, z.imag))


def extract_one(name):
    x, fs = load_wav(SIGNAL)
    x = x[:, 0]
    lay = load_layout(LAYOUT)
    cap, ai = load_aligned(os.path.join(CAPDIR, name), x, fs, lay)
    ref, ri = load_aligned(BYPASS, x, fs, lay)
    off = float((ai.get("ref_gain_db") or 0.0) - (ri.get("ref_gain_db") or 0.0))
    sg = float(ai.get("ref_gain_db") or 0.0)
    d = {"name": name, "fs": fs, "send_offset_db": off, "send_gain_db": sg,
         "params": params_from_name(name)}

    # ---- ramp: the static curve, 2 ms steps
    s = seg(lay, "ramp")
    t0, t1 = s["start"] + 0.05, s["end"] - 0.02
    ec = tone_env(cut(cap[:, 0], fs, t0, t1), fs, 1000.0, 50.0)
    er = tone_env(cut(ref[:, 0], fs, t0, t1), fs, 1000.0, 50.0)
    ex = tone_env(cut(cap[:, 2], fs, t0, t1), fs, 1000.0, 50.0)
    n = min(len(ec), len(er), len(ex))
    step = int(round(0.002 * fs))
    i = np.arange(0, n, step)
    d["ramp_x_dbfs"] = db(ex[i]) - sg
    d["ramp_out_db"] = db(ec[i])
    d["ramp_chain_db"] = db(er[i]) + off
    d["ramp_t_s"] = i / fs

    # ---- bursts: gain trajectory, 0.1 ms resolution, wide detector
    s = seg(lay, "bursts")
    trajs = []
    for tb in s["burst_times"]:
        a, b = tb - 0.01, tb + 0.30
        ec = tone_env(cut(cap[:, 0], fs, a, b), fs, 1000.0, 600.0)
        er = tone_env(cut(ref[:, 0], fs, a, b), fs, 1000.0, 600.0)
        m = min(len(ec), len(er))
        trajs.append(np.stack([np.arange(m) / fs - 0.01, db(ec[:m]) - db(er[:m]) - off,
                               db(er[:m])], axis=0))
    ml = min(t.shape[1] for t in trajs)
    d["burst_traj"] = np.stack([t[:, :ml] for t in trajs])   # (4, 3, ml): t, gain_db, ref_db

    # ---- burst gaps: envelope of the device's own floor (release)
    gaps = []
    times = list(s["burst_times"])
    for k, tb in enumerate(times):
        te = tb + s.get("len_s", 0.06)
        ts = (times[k + 1] - 0.05) if k + 1 < len(times) else te + 1.6
        w = int(round(0.020 * fs))
        a, b = int(te * fs), int(ts * fs)
        tt, vc, vr = [], [], []
        for j in range(a, b - w, w // 2):
            tt.append((j + w / 2) / fs - te)
            vc.append(np.sqrt(np.mean(cap[j:j + w, 0] ** 2)))
            vr.append(np.sqrt(np.mean(ref[j:j + w, 0] ** 2)))
        gaps.append(np.stack([np.array(tt), db(vc), db(vr)]))
    ml = min(g.shape[1] for g in gaps)
    d["gap_traj"] = np.stack([g[:, :ml] for g in gaps])

    # ---- steady sines + THD
    for nm in ("sine20", "sine40"):
        s = seg(lay, nm)
        a, b = s["start"] + 0.5, s["end"] - 0.1
        ec = tone_env(cut(cap[:, 0], fs, a, b), fs, 1000.0, 20.0)
        er = tone_env(cut(ref[:, 0], fs, a, b), fs, 1000.0, 20.0)
        m = min(len(ec), len(er))
        d[nm + "_gain_db"] = float(db(np.median(ec[:m])) - db(np.median(er[:m])) - off)
        # THD: harmonics 2..8 over fundamental, on both
        for tag, sig in (("cap", cap[:, 0]), ("ref", ref[:, 0])):
            y = cut(sig, fs, a, b)
            y = y[: int(len(y) // 1 * 1)]
            w = np.hanning(len(y))
            Y = np.abs(np.fft.rfft(y * w))
            f = np.fft.rfftfreq(len(y), 1.0 / fs)
            def amp(fc):
                k = (f > fc - 30) & (f < fc + 30)
                return float(np.sqrt(np.sum(Y[k] ** 2)))
            h1 = amp(1000.0)
            hn = np.sqrt(sum(amp(1000.0 * k) ** 2 for k in range(2, 9)))
            d[f"{nm}_thd_{tag}_pct"] = 100.0 * hn / max(h1, 1e-20)
        # onset of sine40: still-recovering test
        s40 = seg(lay, "sine40")
    s = seg(lay, "sine40")
    ec = tone_env(cut(cap[:, 0], fs, s["start"], s["end"] - 0.05), fs, 1000.0, 60.0)
    er = tone_env(cut(ref[:, 0], fs, s["start"], s["end"] - 0.05), fs, 1000.0, 60.0)
    m = min(len(ec), len(er))
    st = int(round(0.001 * fs))
    d["sine40_onset"] = np.stack([np.arange(0, m, st) / fs,
                                  db(ec[np.arange(0, m, st)]) - db(er[np.arange(0, m, st)]) - off])
    # onset of sine20 (attack at -20 from rest)
    s = seg(lay, "sine20")
    ec = tone_env(cut(cap[:, 0], fs, s["start"], s["start"] + 1.0), fs, 1000.0, 600.0)
    er = tone_env(cut(ref[:, 0], fs, s["start"], s["start"] + 1.0), fs, 1000.0, 600.0)
    m = min(len(ec), len(er))
    d["sine20_onset"] = np.stack([np.arange(m) / fs, db(ec[:m]) - db(er[:m]) - off])
    # release after sine20 stops: the tail on the device floor
    s = seg(lay, "sine20")
    w = int(round(0.020 * fs))
    a, b = int(s["end"] * fs), int((s["end"] + 1.4) * fs)
    tt, vc, vr = [], [], []
    for j in range(a, b - w, w // 2):
        tt.append((j + w / 2) / fs - s["end"])
        vc.append(np.sqrt(np.mean(cap[j:j + w, 0] ** 2)))
        vr.append(np.sqrt(np.mean(ref[j:j + w, 0] ** 2)))
    d["sine20_tail"] = np.stack([np.array(tt), db(vc), db(vr)])

    # ---- sweep: gain vs frequency
    s = seg(lay, "sweep")
    from analysis.util import welch_bands
    yc = cut(cap[:, 0], fs, s["start"] + 0.05, s["end"] - 0.05)
    yr = cut(ref[:, 0], fs, s["start"] + 0.05, s["end"] - 0.05)
    m = min(len(yc), len(yr))
    fc, mc = welch_bands(yc[:m], fs, nfft=8192, n_bands=48)
    _, mr = welch_bands(yr[:m], fs, nfft=8192, n_bands=48)
    d["sweep_f"] = fc
    d["sweep_gain_db"] = db(mc) - db(mr) - off
    # noise
    s = seg(lay, "noise")
    yc = cut(cap[:, 0], fs, s["start"] + 0.1, s["end"] - 0.1)
    yr = cut(ref[:, 0], fs, s["start"] + 0.1, s["end"] - 0.1)
    m = min(len(yc), len(yr))
    fc, mc = welch_bands(yc[:m], fs, nfft=8192, n_bands=48)
    _, mr = welch_bands(yr[:m], fs, nfft=8192, n_bands=48)
    d["noise_f"] = fc
    d["noise_gain_db"] = db(mc) - db(mr) - off
    d["noise_gain_bb_db"] = float(db(np.sqrt(np.mean(yc[:m] ** 2))) - db(np.sqrt(np.mean(yr[:m] ** 2))) - off)

    # ---- clicks
    s = seg(lay, "clicks")
    gs, shapes = [], []
    for tc in s["click_times"]:
        a = int(round((tc - 0.003) * fs)); b = int(round((tc + 0.25) * fs))
        pc = np.abs(cap[a:b, 0]).max(); pr = np.abs(ref[a:b, 0]).max()
        gs.append(float(db(pc) - db(pr) - off))
    d["click_gain_db"] = np.array(gs)
    # click recovery: envelope of the device floor after the last click
    return d


def _job(name):
    for v in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS",
              "VECLIB_MAXIMUM_THREADS", "NUMEXPR_NUM_THREADS"):
        os.environ.setdefault(v, "1")
    d = extract_one(name)
    p = os.path.join(OUT, name.replace(".wav", ".npz"))
    arrs = {k: v for k, v in d.items() if isinstance(v, np.ndarray)}
    meta = {k: v for k, v in d.items() if not isinstance(v, np.ndarray)}
    np.savez_compressed(p, _meta=json.dumps(meta), **arrs)
    return p


def load(name):
    z = np.load(os.path.join(OUT, name.replace(".wav", ".npz")), allow_pickle=False)
    d = json.loads(str(z["_meta"]))
    for k in z.files:
        if k != "_meta":
            d[k] = z[k]
    return d


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    if sys.argv[1] == "extract":
        from concurrent.futures import ProcessPoolExecutor
        names = comp_captures()
        with ProcessPoolExecutor(max_workers=11) as ex:
            for p in ex.map(_job, names):
                print(p)


# ----------------------------------------------------------------- modelling

MODEL = os.path.join(ROOT, "models", "ax30g-comp.json")


def _measure(y, ref, fs, lay, stim=None):
    """The same measurements extract_one makes, on a rendered pair."""
    d = {}
    s = seg(lay, "ramp")
    t0, t1 = s["start"] + 0.05, s["end"] - 0.02
    ec = tone_env(cut(y[:, 0], fs, t0, t1), fs, 1000.0, 50.0)
    er = tone_env(cut(ref[:, 0], fs, t0, t1), fs, 1000.0, 50.0)
    ex = tone_env(cut(stim, fs, t0, t1), fs, 1000.0, 50.0)
    n = min(len(ec), len(er), len(ex))
    step = int(round(0.002 * fs)); i = np.arange(0, n, step)
    d["ramp_x_dbfs"] = db(ex[i])
    d["ramp_gain_db"] = db(ec[i]) - db(er[i])
    s = seg(lay, "bursts")
    trajs = []
    for tb in s["burst_times"]:
        a, b = tb - 0.01, tb + 0.30
        e1 = tone_env(cut(y[:, 0], fs, a, b), fs, 1000.0, 600.0)
        e2 = tone_env(cut(ref[:, 0], fs, a, b), fs, 1000.0, 600.0)
        m = min(len(e1), len(e2))
        trajs.append(np.stack([np.arange(m) / fs - 0.01, db(e1[:m]) - db(e2[:m])]))
    ml = min(t.shape[1] for t in trajs)
    d["burst_traj"] = np.stack([t[:, :ml] for t in trajs])
    for nm in ("sine20", "sine40"):
        s = seg(lay, nm); a, b = s["start"] + 0.5, s["end"] - 0.1
        e1 = tone_env(cut(y[:, 0], fs, a, b), fs, 1000.0, 20.0)
        e2 = tone_env(cut(ref[:, 0], fs, a, b), fs, 1000.0, 20.0)
        m = min(len(e1), len(e2))
        d[nm + "_gain_db"] = float(db(np.median(e1[:m])) - db(np.median(e2[:m])))
    s = seg(lay, "noise")
    y1 = cut(y[:, 0], fs, s["start"] + 0.1, s["end"] - 0.1)
    y2 = cut(ref[:, 0], fs, s["start"] + 0.1, s["end"] - 0.1)
    m = min(len(y1), len(y2))
    d["noise_gain_bb_db"] = float(db(np.sqrt(np.mean(y1[:m] ** 2))) - db(np.sqrt(np.mean(y2[:m] ** 2))))
    s = seg(lay, "clicks")
    gs = []
    for tc in s["click_times"]:
        a = int(round((tc - 0.003) * fs)); b = int(round((tc + 0.25) * fs))
        gs.append(float(db(np.abs(y[a:b, 0]).max()) - db(np.abs(ref[a:b, 0]).max())))
    d["click_gain_db"] = np.array(gs)
    # sine20 release tail, same 20 ms windows as extract_one
    s = seg(lay, "sine20"); w = int(round(0.020 * fs))
    a, b = int(s["end"] * fs), int((s["end"] + 1.4) * fs)
    tt, v1, v2 = [], [], []
    for j in range(a, b - w, w // 2):
        tt.append((j + w / 2) / fs - s["end"])
        v1.append(np.sqrt(np.mean(y[j:j + w, 0] ** 2)))
        v2.append(np.sqrt(np.mean(ref[j:j + w, 0] ** 2)))
    d["sine20_tail"] = np.stack([np.array(tt), db(v1), db(v2)])
    return d


_CACHE = {}


def render_measure(params, overrides=None, spec_path=MODEL):
    """Render one COMP setting and measure it against the model's own bypass
    (the same spec with the gain forced to unity), so the returned gains are
    directly comparable with extract_one's capture/bypass ratios."""
    import json as _json
    from engine.render import load_spec, render_spec
    import engine.comp                                   # registers COMP
    from analysis.util import load_wav as _lw
    key = "sig"
    if key not in _CACHE:
        x, fs = _lw(SIGNAL); _CACHE[key] = (x[:, 0], fs)
    x, fs = _CACHE[key]
    lay = load_layout(LAYOUT)
    base = load_spec(spec_path)
    if "ref" not in _CACHE:
        rs = _json.loads(_json.dumps(base))
        rs["maps"]["a"] = {"type": "constant", "value": 1.0}
        rs["maps"]["B"] = {"type": "constant", "value": 0.0}
        rs["blocks"]["pivot_amplitude"] = 1e9      # b = B - a/Q -> 0
        ry, _ = render_spec(rs, x, fs, spec_path=spec_path)
        _CACHE["ref"] = ry
    spec = _json.loads(_json.dumps(base))
    spec["params"].update(params)
    for k, v in (overrides or {}).items():
        cur = spec
        parts = k.split(".")
        for q in parts[:-1]:
            cur = cur.setdefault(q, {})
        cur[parts[-1]] = v
    y, truth = render_spec(spec, x, fs, spec_path=spec_path)
    d = _measure(y, _CACHE["ref"], fs, lay, stim=x)
    d["truth"] = {k: v for k, v in truth.items() if k != "input_stage"}
    return d


def _score_job(job):
    for v in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS",
              "VECLIB_MAXIMUM_THREADS", "NUMEXPR_NUM_THREADS"):
        os.environ.setdefault(v, "1")
    params, ov = job
    d = render_measure(params, ov)
    keep = ("ramp_x_dbfs", "ramp_gain_db", "sine20_gain_db", "sine40_gain_db",
            "noise_gain_bb_db", "click_gain_db", "burst_traj", "sine20_tail")
    return {k: d[k] for k in keep}


def ramp_at(d, lv, model=True):
    x = d["ramp_x_dbfs"]
    g = d["ramp_gain_db"] if model else (d["ramp_out_db"] - d["ramp_chain_db"])
    o = np.argsort(x)
    return np.interp(lv, x[o], g[o])


LV = np.arange(-38.0, -0.99, 0.5)


def fit_ab(params, ov, cap, lv=LV, a0=None, b0=None, tol=2e-3, maxit=14, workers=3):
    """Fit (a, b) for one setting by rendering, against the capture's ramp
    curve and its two steady sines. Gauss-Newton on the two parameters with a
    numerical Jacobian; each evaluation is one render."""
    from concurrent.futures import ProcessPoolExecutor
    tgt = np.concatenate([ramp_at(cap, lv, model=False),
                          [cap["sine20_gain_db"], cap["sine40_gain_db"]]])

    def pack(d):
        return np.concatenate([ramp_at(d, lv), [d["sine20_gain_db"], d["sine40_gain_db"]]])

    def mk(a, b):
        o = dict(ov)
        o["maps.a"] = {"type": "constant", "value": float(a)}
        o["maps.b"] = {"type": "constant", "value": float(b)}
        return (params, o)

    a, b = a0, b0
    hist = []
    for it in range(maxit):
        jobs = [mk(a, b), mk(a * 1.02, b), mk(a, b * 1.02)]
        if workers > 1:
            with ProcessPoolExecutor(max_workers=workers) as ex:
                r = list(ex.map(_score_job, jobs))
        else:
            r = [_score_job(j) for j in jobs]
        f0 = pack(r[0]) - tgt
        J = np.stack([(pack(r[1]) - pack(r[0])) / (0.02 * a),
                      (pack(r[2]) - pack(r[0])) / (0.02 * b)], axis=1)
        rms = float(np.sqrt(np.mean(f0 ** 2)))
        hist.append((a, b, rms))
        step = np.linalg.lstsq(J, -f0, rcond=None)[0]
        na, nb = a + step[0], b + step[1]
        if not (na > 0 and nb > 0):
            na, nb = max(na, a / 2), max(nb, b / 2)
        if abs(na - a) / a < tol and abs(nb - b) / b < tol:
            a, b = na, nb
            break
        a, b = na, nb
    d = _score_job(mk(a, b))
    f = pack(d) - tgt
    return {"a": float(a), "b": float(b),
            "rms_db": float(np.sqrt(np.mean(f ** 2))),
            "max_db": float(np.max(np.abs(f))),
            "sine20_err": float(d["sine20_gain_db"] - cap["sine20_gain_db"]),
            "sine40_err": float(d["sine40_gain_db"] - cap["sine40_gain_db"]),
            "noise_err": float(d["noise_gain_bb_db"] - cap["noise_gain_bb_db"]),
            "click_err": float(np.median(d["click_gain_db"]) - np.median(cap["click_gain_db"])),
            "iters": len(hist), "hist": hist, "meas": d}
