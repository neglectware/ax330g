import os, sys, json, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from meas import *

out_p = os.path.join(H2, "features.json")
F = json.load(open(out_p)) if os.path.exists(out_p) else {}
names = sorted(os.path.basename(p)[:-4] for p in glob.glob(os.path.join(H2, "cache", "*.npy")))
for n in names:
    if n in F:
        continue
    y = load(n)
    r = {}
    r["h_sine20"] = harmonics(y)
    r["h_sine40"] = harmonics(y, 27.2, 28.6)
    rp = seg(LAY, "ramp")
    r["ramp"] = tone_track(y, rp["start"], rp["end"], nper=4096, hop=1024)
    s20 = seg(LAY, "sine20")
    r["sine20_track"] = tone_track(y, s20["start"] - 0.3, s20["end"] + 0.6, nper=2048, hop=1024)
    ck = seg(LAY, "clicks")["click_times"]
    r["ring_hz"] = [ring(y, t) for t in ck]
    r["ring_decay"] = [ring_decay(y, t) for t in ck]
    r["noise"] = noise_tf(y)
    r["sweep_resp"] = sweep_resp(y)
    di = seg(LAY, "di")
    r["di_track"] = peak_track(y, di["start"], di["end"])
    bt = seg(LAY, "bursts")["burst_times"]
    r["burst_tracks"] = [peak_track(y, b - 0.05, b + 1.9) for b in bt]
    r["ramp_track"] = peak_track(y, rp["start"], rp["end"] + 1.0)
    F[n] = r
    json.dump(F, open(out_p, "w"))
    print("ok", n[11:], flush=True)
