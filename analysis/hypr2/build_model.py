"""Write models/ax30g-hypr-2.json from the old model + the 2026-09-23 fits."""
import json, os, sys, math
import numpy as np
H2 = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # the project root
m = json.load(open(os.path.join(ROOT, "models", "ax30g-hypr.json")))
sw = json.load(open(os.path.join(H2, "sweep_fit.json")))["params"]
sens = json.load(open(os.path.join(H2, "sens_fit.json")))
drv = json.load(open(os.path.join(H2, "driver_fit2.json")))
sq = json.load(open(os.path.join(H2, "eval_driver.json")))
FSD = 39062.5
fc_of = lambda F: FSD / math.pi * math.asin(min(F / 2, 1.0))
HS = [0, 5, 10, 25, 37, 50]

m["name"] = "AX30G Hyper Resonator (HYPR), re-fitted 2026-09-23 from capture/grids/hypr-2.json"
m["maps"]["q"] = {"type": "interp", "interp": "inverse",
                  "points": [[0, 4.13], [10, 4.94], [25, 7.25], [37, 13.1], [50, 140.0]]}
for k in ("sens", "depth", "attack_tau_ms", "release_tau_ms"):
    m["maps"].pop(k, None)
m["maps"]["decay_tau_ms"] = {"type": "interp_log", "points": [[D, round(sw["dec%d" % D], 2)] for D in (0, 10, 25, 37, 50)]}
# S0/S37/S50 are fitted (flat ranges 18.75-19.25, 3.50-4.00, 2.00-2.50 dB); S10 and S25 are only
# bounded (5.25-18.50 dB each) and are set on the straight line between S0 and S37, inside that range
m["maps"]["sens_hysteresis_db"] = {"type": "interp", "points": [[0, 18.75], [10, 14.5], [25, 9.0], [37, 3.75], [50, 2.25]]}
b = m["blocks"]
b.pop("detector", None)
b["sweep"] = {
    "mode": "trigger_eg",
    "rest_hz": 389.0,
    "down_rest_hz": round(fc_of(sw["F_top"]), 1),
    "F_per_depth_up": round(sw["k_up"], 6),
    "F_per_depth_down": round(sw["k_dn"], 6),
    "min_hz": 40.0,
    "max_hz": round(fc_of(sw["F_max"]), 1),
    "trigger": {"threshold_dbfs": round(sw["T_on_db"], 2), "detector_release_ms": round(sw["tau_det_ms"], 2)},
    "eg": {"attack_ms": round(sw["tau_att_ms"], 3), "attack_len_ms": round(sw["t_sw_ms"], 2)},
}
HSs = [str(H) for H in HS]
tbl = {"1": {"gain_db": [[H, drv["odd"]["1,%d" % H][0]] for H in HS],
             "w_odd": [[H, round(10 ** (drv["odd"]["1,%d" % H][1] / 20), 7)] for H in HS],
             "w_even": [[0, 0.0], [50, 0.0]]},
       "2": {"odd_mode": "square_env", "knee_dbfs": sq["knee_dbfs"], "env_release_ms": 10.0,
             "gain_db": [[0, 0.0], [50, 0.0]],
             "w_odd": [[H, round(10 ** (sq["w_db"][str(H)] / 20), 7)] for H in HS],
             "w_even": [[0, 0.0], [50, 0.0]]}}
b["driver"]["table"] = tbl
b["driver"]["even"] = {"gain_db": [[H, drv["even"][str(H)][0]] for H in HS],
                       "w": [[H, round(10 ** (drv["even"][str(H)][1] / 20), 7)] for H in HS]}
b["resonator"]["q_rest_for_driver_fit"] = 4.13
b["resonator"]["gain_db"] = 5.47
b["resonator"]["notes"] = ("The driver weights are fitted directly in capture dBFS; gain_db +5.47 puts the effect path on the same "
    "footing as the Direct path, which renders 5.47 dB above the capture (the 09-18 null test's fitted gain on the "
    "Direct-only row, a pre-existing offset of the Direct path, kept so the Direct/Effect balance is right).")
m["notes"] = [
    "Re-fit 2026-09-23 from the 35-row hypr-2 campaign; derivation, fit/held-out split and every residual: docs/hypr-model-2-2026-09-23.md. The 09-18 model (models/ax30g-hypr.json) is unchanged and still renders bit-identically.",
    "SWEEP: not an envelope follower. A Schmitt trigger on a peak detector (instant attack, release blocks.sweep.trigger.detector_release_ms) fires an attack/decay envelope g; the corner is F = 2 sin(pi fc/fs) = F(389 Hz) + F_per_depth_up*Depth*g (UP) or F(down_rest_hz) - F_per_depth_down*Depth*g (DOWN). Decay is the envelope's decay time constant (maps.decay_tau_ms, interpolated in log). Sensitivity is the trigger's hysteresis (maps.sens_hysteresis_db), not a gain or threshold.",
    "Polarity DOWN rests at the TOP (down_rest_hz) and sweeps down; UP rests at 389 Hz and sweeps up.",
    "Resonance: Q interpolated in 1/Q (damping), which the five measured points are close to linear in.",
    "DRIVER: odd branch per (Type, Harmonics), even branch shared by both Types with its own gain (blocks.driver.even). The level-dependent notches in the even and odd harmonics (docs, section 4) are NOT modelled.",
    "Effect Level is linear in the displayed value, measured (-12.37/-6.00/-2.60 dB at 12/25/37 against 50).",
]
json.dump(m, open(os.path.join(ROOT, "models", "ax30g-hypr-2.json"), "w"), indent=1)
print(json.dumps(b["sweep"], indent=1)); print(m["maps"]["decay_tau_ms"], m["maps"]["sens_hysteresis_db"])
