// AX30G analog input stage, measured constants (models/ax30g-input-stage.json,
// docs/input-stage-2026-09-16.md). kOversample is kept only for documentation
// parity with the JSON's "ceiling.oversample" -- section 7/10 of that doc
// settled on 1 (clip on the sample grid, not oversampled) and dsp/input_stage.h
// does not implement the N>1 branch engine/render.py::apply_input_stage has.
// Generated verbatim from the JSON with:
//   .venv/bin/python3 -c "
//   import json
//   d = json.load(open('models/ax30g-input-stage.json'))
//   c = d['ceiling']; pe = d['pre_emphasis']['discrete_39062_5']; peak = d['peak_led']
//   print('static constexpr double kHeadroomDb = %r;' % c['headroom_dbfs'])
//   print('static constexpr double kOffsetFrac = %r;' % c['offset_frac'])
//   print('static constexpr int kOversample = %d;' % c['oversample'])
//   print('static constexpr double kPreB0 = %r;' % pe['b'][0])
//   print('static constexpr double kPreB1 = %r;' % pe['b'][1])
//   print('static constexpr double kPreA1 = %r;' % pe['a'][1])
//   print('static constexpr double kPeakLedMarginDb = %r;' % peak['margin_db'])
//   "
// Do not hand-edit; regenerate the same way if the JSON model ever changes.
#pragma once

namespace ax30g {

static constexpr double kHeadroomDb = 2.0605;
static constexpr double kOffsetFrac = 0.027;
static constexpr int kOversample = 1;
static constexpr double kPreB0 = 2.322379702368948;
static constexpr double kPreB1 = -1.5482677728662766;
static constexpr double kPreA1 = -0.22588807049732856;
static constexpr double kPeakLedMarginDb = -1.0;

} // namespace ax30g
