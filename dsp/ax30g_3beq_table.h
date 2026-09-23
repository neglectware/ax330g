// AX30G 3-Band EQ (3BEQ) measured constants (models/ax30g-3beq.json,
// docs/3beq-model-2026-09-17.md, docs/3beq-cpp-spec.md Sec 6). Generated
// verbatim from the JSON, the way dsp/input_stage_table.h is generated from
// its own JSON. Do not hand-edit; regenerate the same way if the JSON model
// ever changes.
//   .venv/bin/python3 -c "
//   import json
//   d = json.load(open('models/ax30g-3beq.json'))
//   mb = d['maps']['bass']; mm = d['maps']['mid']; mt = d['maps']['treble']
//   bl = d['blocks']['clip']; pa = d['blocks']['path']
//   print('static constexpr double kBassHz = %r;' % mb['corner_hz'])
//   print('static constexpr double kBassOffBoostDb = %r;' % mb['gain_offset_boost_db'])
//   print('static constexpr double kBassOffCutDb = %r;' % mb['gain_offset_cut_db'])
//   print('static constexpr double kTrebleHz = %r;' % mt['corner_hz'])
//   print('static constexpr double kTrebleOffBoostDb = %r;' % mt['gain_offset_boost_db'])
//   print('static constexpr double kTrebleOffCutDb = %r;' % mt['gain_offset_cut_db'])
//   print('static constexpr double kMidQBoost = %r;' % mm['q_boost'])
//   print('static constexpr double kMidQCut = %r;' % mm['q_cut'])
//   print('static constexpr double kMidOffBoostDb = %r;' % mm['gain_offset_boost_db'])
//   print('static constexpr double kMidOffCutDb = %r;' % mm['gain_offset_cut_db'])
//   print('static constexpr double kClipLevel = %r;' % bl['level'])
//   print('static constexpr double kPathGainDb = %r;' % pa['gain_db'])
//   print('static constexpr int kPathDelaySamples = %d;' % pa['delay_samples'])
//   print('static constexpr int kMidFreqSteps[13] = {' + ', '.join(str(v) for v in d['mid_freq_steps_hz']) + '};')
//   "
#pragma once

namespace ax30g {

static constexpr double kBassHz = 79.856;
static constexpr double kBassOffBoostDb = 0.0465;
static constexpr double kBassOffCutDb = -0.0038;
static constexpr double kTrebleHz = 7997.0;
static constexpr double kTrebleOffBoostDb = 0.0008;
static constexpr double kTrebleOffCutDb = -0.0065;
static constexpr double kMidQBoost = 0.9858;
static constexpr double kMidQCut = 2.9789;
static constexpr double kMidOffBoostDb = -0.0168;
static constexpr double kMidOffCutDb = -0.0159;
static constexpr double kClipLevel = 0.93;
static constexpr double kPathGainDb = -0.358;
static constexpr int kPathDelaySamples = 1;
static constexpr int kMidFreqSteps[13] = {250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000};

} // namespace ax30g
