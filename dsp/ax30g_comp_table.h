// AX30G Compressor (COMP) measured constants (models/ax30g-comp.json,
// docs/comp-model-2026-09-18.md, docs/comp-cpp-spec.md Sec 7). Generated
// verbatim from the JSON, the way dsp/input_stage_table.h is generated from
// its own JSON. Do not hand-edit; regenerate the same way if the JSON model
// ever changes.
//   .venv/bin/python3 -c "
//   import json
//   d = json.load(open('models/ax30g-comp.json'))
//   m = d['maps']; b = d['blocks']; det = b['detector']
//   a = m['a']['points']; at = m['attack_tau_ms']['points']
//   print('static constexpr double kCompPivotQ = %r;' % b['pivot_amplitude'])
//   print('static constexpr double kCompLevelC = %r;' % m['B']['c'])
//   print('static constexpr double kCompLevelD = %r;' % m['B']['d'])
//   print('static constexpr double kCompEnvFloor = %r;' % det['envelope_floor'])
//   print('static constexpr int kCompSensPoints = %d;' % len(a))
//   print('static constexpr double kCompSensX[%d] = {%s};' % (len(a), ', '.join(repr(float(p[0])) for p in a)))
//   print('static constexpr double kCompSensA[%d] = {%s};' % (len(a), ', '.join(repr(p[1]) for p in a)))
//   print('static constexpr int kCompAttackPoints = %d;' % len(at))
//   print('static constexpr double kCompAttackX[%d] = {%s};' % (len(at), ', '.join(repr(float(p[0])) for p in at)))
//   print('static constexpr double kCompAttackTauMs[%d] = {%s};' % (len(at), ', '.join(repr(p[1]) for p in at)))
//   print('static constexpr double kCompDetectorAttackTauMs = %r;' % det['attack_tau_ms'])
//   print('static constexpr double kCompDetectorReleaseTauMs = %r;' % det['release_tau_ms'])
//   "
//
// The law (docs/comp-cpp-spec.md Sec 1/6): y[n] = x[n] / (a + b*E[n]), E the
// peak envelope of the pre-emphasised mono signal. a = interp(kCompSensX/A,
// Sensitivity); B = kCompLevelC / (Level + kCompLevelD), Level 0 handled as
// a mute BEFORE this (no B, no detector); b = B - a / kCompPivotQ (NOT
// tabulated -- the measured constraint across the Sensitivity axis, one
// constant to 0.1 %). Attack resolves a SMOOTHER time constant applied
// after the (fixed) detector, via kCompAttackX/TauMs; the detector's own
// attack/release (kCompDetectorAttackTauMs/kCompDetectorReleaseTauMs) do
// NOT move with any control.
#pragma once

namespace ax30g {

static constexpr double kCompPivotQ = 0.131198;
static constexpr double kCompLevelC = 407.111;
static constexpr double kCompLevelD = 2.66643;
static constexpr double kCompEnvFloor = 0.008;

static constexpr int kCompSensPoints = 5;
static constexpr double kCompSensX[5] = {0.0, 10.0, 25.0, 40.0, 50.0};
static constexpr double kCompSensA[5] = {1.03514, 0.92049, 0.69295, 0.35482, 0.02177};

static constexpr int kCompAttackPoints = 3;
static constexpr double kCompAttackX[3] = {0.0, 25.0, 50.0};
static constexpr double kCompAttackTauMs[3] = {1.5, 1.75, 11.8};

static constexpr double kCompDetectorAttackTauMs = 1.2;
static constexpr double kCompDetectorReleaseTauMs = 48.0;

} // namespace ax30g
