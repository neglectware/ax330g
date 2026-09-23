"""Stage 1: align every HYPR capture once, cache ch0 (and ch1) + reference as float32, plus
reference diagnostics. Run under nice with threads=1."""
import os, sys, json, glob
import numpy as np
ROOT = "/path/to/ax30g"
sys.path.insert(0, ROOT)
from analysis.util import load_wav, load_layout, seg, cut
from analysis.run import load_aligned

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "cache")
os.makedirs(OUT, exist_ok=True)
x, fs = load_wav(os.path.join(ROOT, "capture", "signalset-normal.wav"))
x = x[:, 0]
L = load_layout(os.path.join(ROOT, "capture", "layout-normal.json"))

def db(v): return float(20 * np.log10(max(v, 1e-15)))

paths = sorted(glob.glob(os.path.join(ROOT, "captures", "AX30G_HYPR_*.wav"))) + \
        [os.path.join(ROOT, "captures", "AX30G_BYPASS_IN-LIN.wav"), os.path.join(ROOT, "captures", "AX30G_BYPASS_IN-MAX.wav")]
diag = {}
dp = os.path.join(OUT, "diag.json")
if os.path.exists(dp):
    diag = json.load(open(dp))
for p in paths:
    name = os.path.splitext(os.path.basename(p))[0]
    if name in diag and os.path.exists(os.path.join(OUT, name + ".npy")):
        continue
    try:
        cap, ai = load_aligned(p, x, fs, L)
    except Exception as e:
        diag[name] = {"error": repr(e)}
        continue
    arr = cap.astype(np.float32)
    np.save(os.path.join(OUT, name + ".npy"), arr)
    d = dict(ai)
    d["mtime"] = os.path.getmtime(p)
    if arr.shape[1] >= 3:
        r = arr[:, 2].astype(float)
        d["ref_seg_rms_db"] = {}
        d["ref_seg_peak_db"] = {}
        for s in L["segments"]:
            c = cut(r, fs, s["start"], s["end"])
            xs = cut(x, fs, s["start"], s["end"])
            d["ref_seg_rms_db"][s["name"]] = db(np.sqrt(np.mean(c ** 2))) - db(np.sqrt(np.mean(xs ** 2)) + 1e-15)
            d["ref_seg_peak_db"][s["name"]] = db(np.max(np.abs(c)))
        # null of reference against the signal set (scaled), per segment
        g = np.dot(r[:len(x)], x) / np.dot(x, x)
        res = r[:len(x)] - g * x
        d["ref_gain_fit_db"] = db(abs(g))
        d["ref_null_db"] = {}
        for s in L["segments"]:
            if s["name"] == "silence":
                continue
            c = cut(res, fs, s["start"], s["end"]); xs = cut(g * x, fs, s["start"], s["end"])
            d["ref_null_db"][s["name"]] = db(np.sqrt(np.mean(c ** 2))) - db(np.sqrt(np.mean(xs ** 2)))
    y = arr[:, 0].astype(float)
    d["seg_rms_db"] = {s["name"]: db(np.sqrt(np.mean(cut(y, fs, s["start"], s["end"]) ** 2))) for s in L["segments"]}
    if arr.shape[1] >= 2:
        d["LR_diff_db"] = db(np.sqrt(np.mean((arr[:, 0] - arr[:, 1]).astype(float) ** 2))) - db(np.sqrt(np.mean(y ** 2)))
    diag[name] = d
    json.dump(diag, open(dp, "w"), indent=1, default=float)
    print(name[11:], "ref_gain %.2f peak %.1f click_dis %.3f ms drops %s" % (d.get("ref_gain_db", 0), d.get("ref_peak_dbfs", 0), d.get("click_disagreement_ms", 0), d.get("dropouts_s")), flush=True)
json.dump(diag, open(dp, "w"), indent=1, default=float)
