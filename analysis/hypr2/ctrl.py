"""Vectorised control path of the trigger-EG sweep model (device rate).

detector  E[n] = max(|x[n]|, a_r * E[n-1])          (instant attack, exponential release tau_det)
trigger   armed & E >= T_on   -> fire, disarm;  re-arm when E < T_on * 10^(-hyst_db/20)
EG        on fire: g rises toward 1 with a one-pole attack (tau_att), from its current value;
          after t_hold (= attack settle, 4*tau_att) it decays toward 0 with tau_dec.
          (implemented as: g(t) = 1 - (1-g0) e^{-t/tau_att} for t < t_sw, then exponential decay)
F         F = F_rest + k_up * Depth * g        (UP)
          F = F_top  - k_dn * Depth * g        (DOWN)
          fc = fs/pi * asin(F/2), F clipped to [F_min, F_max]
"""
import numpy as np

FSD = 39062.5


def F_of(fc, fs=FSD):
    return 2 * np.sin(np.pi * np.asarray(fc) / fs)


def fc_of(F, fs=FSD):
    return fs / np.pi * np.arcsin(np.clip(np.asarray(F) / 2, 0, 1))


def detector(x, tau_det_ms, fs=FSD):
    a = np.exp(-1.0 / (tau_det_ms * 1e-3 * fs))
    la = np.log(a)
    n = np.arange(len(x))
    lx = np.log(np.maximum(np.abs(x), 1e-12))
    m = np.maximum.accumulate(lx - n * la)
    return np.exp(m + n * la)


def triggers(E, T_on_db, hyst_db):
    """indices of fire events. Armed at start."""
    on = 10 ** (T_on_db / 20)
    off = 10 ** ((T_on_db - hyst_db) / 20)
    above = E >= on
    below = E < off
    fires = []
    i = 0
    n = len(E)
    armed = True
    while True:
        if armed:
            idx = np.flatnonzero(above[i:])
            if not len(idx):
                break
            i = i + idx[0]
            fires.append(i)
            armed = False
        else:
            idx = np.flatnonzero(below[i:])
            if not len(idx):
                break
            i = i + idx[0]
            armed = True
    return np.array(fires, dtype=int)


def eg(n, fires, tau_att_ms, tau_dec_ms, t_sw_ms=None, fs=FSD, g_init=0.0):
    """Attack toward 1 for t_sw after each fire (one-pole tau_att), then decay with tau_dec."""
    g = np.empty(n)
    ta = max(tau_att_ms, 1e-3) * 1e-3 * fs
    td = max(tau_dec_ms, 1e-3) * 1e-3 * fs
    tsw = int(round((t_sw_ms if t_sw_ms is not None else 4 * tau_att_ms) * 1e-3 * fs))
    bounds = list(fires) + [n]
    cur = g_init
    # before first fire: decay from g_init
    first = bounds[0]
    k = np.arange(first)
    g[:first] = cur * np.exp(-k / td)
    cur = g[first - 1] if first > 0 else cur
    for j in range(len(fires)):
        s, e = fires[j], bounds[j + 1]
        L = e - s
        k = np.arange(L)
        a_len = min(tsw, L)
        g0 = cur
        seg = np.empty(L)
        seg[:a_len] = 1 - (1 - g0) * np.exp(-k[:a_len] / ta)
        if L > a_len:
            gp = seg[a_len - 1] if a_len > 0 else g0
            seg[a_len:] = gp * np.exp(-(k[a_len:] - a_len + 1) / td)
        g[s:e] = seg
        cur = seg[-1]
    return g


def control(x, p, depth, decay_tau_ms, pol="UP", hyst_db=None, fs=FSD):
    E = detector(x, p["tau_det_ms"], fs)
    fires = triggers(E, p["T_on_db"], p["hyst_db"] if hyst_db is None else hyst_db)
    g = eg(len(x), fires, p["tau_att_ms"], decay_tau_ms, p.get("t_sw_ms"), fs)
    if pol == "UP":
        F = p["F_rest"] + p["k_up"] * depth * g
    else:
        F = p["F_top"] - p["k_dn"] * depth * g
    F = np.clip(F, p.get("F_min", 0.0065), p.get("F_max", 1.95))
    return F, E, fires, g
