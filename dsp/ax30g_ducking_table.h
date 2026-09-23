// AX30G delay Ducking (SDLY/XDLY/TDLY/HDLY), measured constants
// (models/ax30g-ducking.json, docs/ducking-model-2026-09-18.md,
// docs/ducking-cpp-spec.md). Generated verbatim from the JSON, the way
// dsp/input_stage_table.h is generated from its own JSON. Do not hand-edit;
// regenerate the same way if the JSON model ever changes.
//   .venv/bin/python3 -c "
//   import json
//   d = json.load(open('models/ax30g-ducking.json'))
//   det = d['detector']; pre = det['prefilter']; pk = det['peak']; g = d['gain']
//   print('static constexpr double kDuckPreB0 = %r;' % pre['b'][0])
//   print('static constexpr double kDuckPreB1 = %r;' % pre['b'][1])
//   print('static constexpr double kDuckPreB2 = %r;' % pre['b'][2])
//   print('static constexpr double kDuckPreA1 = %r;' % pre['a'][1])
//   print('static constexpr double kDuckPreA2 = %r;' % pre['a'][2])
//   print('static constexpr double kDuckPeakAttackMs = %r;' % pk['attack_ms'])
//   print('static constexpr double kDuckPeakReleaseMs = %r;' % pk['release_ms'])
//   print('static constexpr double kDuckSlowAttackMs = %r;' % det['attack_ms'])
//   print('static constexpr double kDuckSlowReleaseMs = %r;' % det['release_ms'])
//   print('static constexpr double kDuckK = %r;' % g['k'])
//   print('static constexpr double kDuckMinGain = %r;' % g['min_gain'])
//   print('static constexpr double kDuckMaxGain = %r;' % g['max_gain'])
//   "
//
// The prefilter is direct form I, docs/ducking-cpp-spec.md Sec 4.1:
//   y[n] = kDuckPreB0*x[n] + kDuckPreB1*x[n-1] + kDuckPreB2*x[n-2]
//        - kDuckPreA1*y[n-1] - kDuckPreA2*y[n-2]
// (kDuckPreA1/A2 carry their JSON sign already, i.e. the same convention
// scipy.signal.lfilter's `a` array uses with a[0]=1 -- do not negate them
// again). Two cascaded real first-order shelves, unity at 1 kHz, fitted at
// 39063.829787 Hz; run unchanged at 39062.5 Hz (34 ppm corner shift, 0.0001
// dB). The peak/slow stages are docs/ducking-cpp-spec.md Sec 4.2's `follow`
// one-pole, coefficients computed from these taus and the running sample
// rate (a = exp(-1/(tau*fs))), not from literals. The gain law is Sec 4.3:
// g = clip(1 - kDuckK*Ducking*env, kDuckMinGain, kDuckMaxGain).
#pragma once

namespace ax30g {

static constexpr double kDuckPreB0 = 1.0749859816;
static constexpr double kDuckPreB1 = -2.0328469525;
static constexpr double kDuckPreB2 = 0.9589679413;
static constexpr double kDuckPreA1 = -1.855669117;
static constexpr double kDuckPreA2 = 0.858168122;
static constexpr double kDuckPeakAttackMs = 0.02;
static constexpr double kDuckPeakReleaseMs = 1.68;
static constexpr double kDuckSlowAttackMs = 52.0;
static constexpr double kDuckSlowReleaseMs = 52.0;
static constexpr double kDuckK = 0.25184;
static constexpr double kDuckMinGain = 0.0;
static constexpr double kDuckMaxGain = 1.0;

} // namespace ax30g
