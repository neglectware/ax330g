PY=.venv/bin/python

.PHONY: venv signals harness harness-eqcomp sim eq3-null

venv:
	uv venv .venv && VIRTUAL_ENV=.venv uv pip install numpy scipy soundfile sounddevice matplotlib

signals:
	$(PY) capture/signals.py

harness:
	$(PY) tests/run_harness.py

sim:
	$(PY) capture/capture.py --grid capture/grids/sdly-m1.json --simulate models/sim/sdly-8bit-mu-half.json --yes --out captures/sim-grid

# LFO-measurement signal set (30 s tone). Overwrites capture/signalset.wav +
# layout.json; relaunch the bench app after either target.
signals-lfo:
	.venv/bin/python capture/signals.py --lfo-set
# 120 s tone for the slow speeds (0.02-0.18 Hz); analyse with
#   tests/modd_trajectory.py <wav> --signal capture/signalset-lfo120.wav --layout capture/layout-lfo120.json
signals-lfo-long:
	.venv/bin/python capture/signals.py --lfo-set --tone-seconds 120
	cp capture/signalset.wav capture/signalset-lfo120.wav
	cp capture/layout.json capture/layout-lfo120.json
signals-normal:
	.venv/bin/python capture/signals.py

# High-feedback tail set (single click/burst/hot-burst/tone, each with a 28 s
# silent tail) -- for Fb ~46-50 SMOD/SDLY loops whose decay outruns the
# normal 57 s set's gaps. Overwrites capture/signalset.wav + layout.json (the
# bench app loads those); relaunch the bench app after. Also copies to
# signalset-tail.wav / layout-tail.json for analysis, mirroring signals-lfo-long.
signals-tail:
	.venv/bin/python capture/signals.py --tail-set
	cp capture/signalset.wav capture/signalset-tail.wav
	cp capture/layout.json capture/layout-tail.json

# Hot broadband tail set for the reverb tank (-6 dBFS click/sweep/burst,
# each with a 28 s silent tail) -- see docs/rev-identification-2026-09-17.md
# section 8: the tail set's -16 dBFS click only has 28 dB of range, which is
# what stopped the structural identification short of the tank. Overwrites
# capture/signalset.wav + layout.json (the bench app loads those); relaunch
# the bench app after. Also copies to signalset-rev.wav / layout-rev.json
# for analysis, mirroring signals-tail. Run `make signals-normal` (or
# -lfo/-tail) afterward before any other grid.
signals-rev:
	.venv/bin/python capture/signals.py --rev-set
	cp capture/signalset.wav capture/signalset-rev.wav
	cp capture/layout.json capture/layout-rev.json

# Synthetic recovery test for analysis/eq_response.py (3BEQ) and
# analysis/comp_curve.py (COMP): renders the normal signal set through a
# known biquad EQ and a known feed-forward compressor, wrapped in a stand-in
# converter chain, and checks the analysis recovers the parameters.
# ~13 s. Needs capture/signalset-normal.wav (make signals-normal).
harness-eqcomp:
	$(PY) tests/comp_eq_synth.py

# 3BEQ: null every captured setting against models/ax30g-3beq.json and rewrite
# docs/3beq-null-2026-09-17.md. ~45 minutes of one core for all 47 rows; pass
# ONLY=<substring> for a single row. Never run this while a capture is going.
eq3-null:
	$(PY) tests/eq3_null.py $(if $(ONLY),--only $(ONLY),) --md docs/3beq-null-$(shell date +%Y-%m-%d).md --json out/null/3beq-grid.json
