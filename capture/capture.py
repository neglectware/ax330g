#!/usr/bin/env python3
"""Capture app: plays the signal set, prompts for the next panel setting,
records the AX30G's L/R output, names the file and appends the CSV log.

    capture.py --list-devices
    capture.py --grid grids/sdly.json --device "Scarlett 18i20" --out ../captures
    capture.py --grid grids/sdly.json --simulate ../models/sim/sdly-clean.json

A grid file is a list of steps: {"effect": "SDLY", "params": {...}, "input": "N"}.
--simulate renders through the Python engine instead of the sound card, which
exercises the whole naming/logging path without hardware.

Naming: AX30G_<EFFECT>_<param>-<val>_..._IN-<N|H>.wav  (spaces removed).

Channels: the signal set is sent on a stereo output pair; one leg feeds the
AX30G (mono in), the other loops straight back into a third input. Recorded
file: ch0 = AX30G L, ch1 = AX30G R, ch2 = reference. The analysis aligns on
the reference, so recording can start whenever.
"""
import argparse
import csv
import datetime as dt
import json
import os
import sys
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))


def fname(effect, params, inp):
    parts = [f"AX30G_{effect}"]
    for k, v in params.items():
        parts.append(f"{k.replace(' ', '')}-{v}")
    parts.append(f"IN-{inp}")
    return "_".join(parts) + ".wav"


def next_available(out_dir, name):
    """`name` if nothing at out_dir/name exists yet, else name with
    `_take2`, `_take3`, ... inserted before the extension -- so a repeated
    capture of the same setting never silently overwrites a previous one.
    Returns (actual_name, take_number)."""
    if not os.path.exists(os.path.join(out_dir, name)):
        return name, 1
    stem, ext = os.path.splitext(name)
    take = 2
    while True:
        candidate = f"{stem}_take{take}{ext}"
        if not os.path.exists(os.path.join(out_dir, candidate)):
            return candidate, take
        take += 1


def record_hw(x, fs, device, in_ch, out_ch, pre=0.5, post=3.0):
    import sounddevice as sd
    sd.default.device = device
    pad = np.zeros(int(pre * fs))
    tail = np.zeros(int(post * fs))
    sig = np.concatenate([pad, x, tail])
    out = np.zeros((len(sig), max(out_ch) + 1), dtype="float32")
    for c in out_ch:
        out[:, c] = sig
    rec = sd.playrec(out, samplerate=fs, channels=max(in_ch) + 1, dtype="float32", blocking=True)
    rec = rec[:, in_ch]
    return rec  # not trimmed: the reference channel is what aligns it


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list-devices", action="store_true")
    ap.add_argument("--grid", help="JSON list of steps")
    ap.add_argument("--signal", default=os.path.join(HERE, "signalset.wav"))
    ap.add_argument("--layout", default=os.path.join(HERE, "layout.json"))
    ap.add_argument("--out", default=os.path.join(os.path.dirname(HERE), "captures"))
    ap.add_argument("--device", default=None, help="sounddevice name or index")
    ap.add_argument("--in-ch", default="0,1,2", help="interface inputs (0-based): AX30G L, AX30G R, reference")
    ap.add_argument("--out-ch", default="0,1", help="output pair carrying the signal set (one leg to the pedal, one back in)")
    ap.add_argument("--simulate", help="model JSON: render through the engine instead of hardware")
    ap.add_argument("--yes", action="store_true", help="do not prompt (simulate mode)")
    a = ap.parse_args()

    if a.list_devices:
        import sounddevice as sd
        print(sd.query_devices())
        return

    if not a.grid:
        ap.error("--grid required")
    with open(a.grid) as f:
        grid = json.load(f)
    x, fs = sf.read(a.signal, dtype="float64", always_2d=True)
    x = x[:, 0]
    os.makedirs(a.out, exist_ok=True)
    log = os.path.join(a.out, "capture_log.csv")
    new = not os.path.exists(log)
    in_ch = [int(c) for c in a.in_ch.split(",")]
    out_ch = [int(c) for c in a.out_ch.split(",")]

    with open(log, "a", newline="") as lf:
        w = csv.writer(lf)
        if new:
            w.writerow(["timestamp", "file", "effect", "input", "params_json", "device", "note"])
        for i, step in enumerate(grid):
            effect = step["effect"]
            params = step["params"]
            inp = step.get("input", "N")
            name, take = next_available(a.out, fname(effect, params, inp))
            path = os.path.join(a.out, name)
            print(f"\n[{i + 1}/{len(grid)}] {effect}  Input Level: {inp}" + (f"  (take {take})" if take > 1 else ""))
            for k, v in params.items():
                print(f"    {k:14s} = {v}")
            if a.simulate:
                from engine import load_spec, render_spec
                spec = load_spec(a.simulate)
                spec = dict(spec)
                spec["params"] = {**spec["params"], **params}
                y, truth = render_spec(spec, x, fs)
                # emulate a hand-started recording: random lead-in, reference on ch2 at -12 dB
                lead = int(np.random.default_rng().uniform(0.5, 6.0) * fs)
                y3 = np.zeros((lead + len(y) + int(2 * fs), 3))
                y3[lead:lead + len(y), :2] = y
                y3[lead:lead + len(x), 2] = x * 10 ** (-12 / 20)
                y = y3
                note = "simulated " + os.path.basename(a.simulate)
                with open(path[:-4] + ".truth.json", "w") as tf:
                    json.dump(truth, tf, indent=1)
            else:
                if not a.yes:
                    r = input("    set the unit, then Enter to capture (s = skip, q = quit): ").strip().lower()
                    if r == "q":
                        break
                    if r == "s":
                        continue
                y = record_hw(x, fs, a.device, in_ch, out_ch)
                note = ""
                peak = float(np.max(np.abs(y)))
                print(f"    peak {20 * np.log10(max(peak, 1e-9)):.1f} dBFS" + ("  ** CLIPPED **" if peak >= 0.999 else ""))
            sf.write(path, y.astype(np.float32), fs, subtype="FLOAT")
            w.writerow([dt.datetime.now().isoformat(timespec="seconds"), name, effect, inp,
                        json.dumps(params), a.device or "sim", note])
            lf.flush()
            print(f"    -> {name}")


if __name__ == "__main__":
    main()
