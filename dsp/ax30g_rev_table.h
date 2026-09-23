// AX30G Reverb (REV) measured constants (models/ax30g-rev.json,
// docs/rev-model-2026-09-18.md, docs/rev-highdamp-2026-09-18.md,
// docs/rev-cpp-spec.md). Generated verbatim from the JSON, the way
// dsp/input_stage_table.h is generated from its own JSON. Do not hand-edit;
// regenerate the same way if the JSON model ever changes.
//   .venv/bin/python3 -c "
//   import json
//   d = json.load(open('models/ax30g-rev.json'))
//   m = d['maps']
//   pre = m['pre_delay']
//   bw = m['balance_wet']['points']; bd = m['balance_dry']['points']
//   hd = m['high_damp']
//   print('static constexpr double kPreDlySamplesPerMs = %r;' % pre['k'])
//   print('static constexpr double kPreDlyOffsetSamples = %r;' % pre['offset_samples'])
//   print('static constexpr int kBalancePoints = %d;' % len(bw))
//   print('static constexpr double kBalanceX[%d] = {%s};' % (len(bw), ', '.join(repr(p[0]) for p in bw)))
//   print('static constexpr double kBalanceWet[%d] = {%s};' % (len(bw), ', '.join(repr(p[1]) for p in bw)))
//   print('static constexpr double kBalanceDry[%d] = {%s};' % (len(bd), ', '.join(repr(p[1]) for p in bd)))
//   print('static constexpr double kCombMix = %r;' % d['comb_mix'])
//   print('static constexpr double kWetPolarity = %r;' % d['wet_polarity'])
//   print('static constexpr double kHighDampInMax = %r;' % hd['in_max'])
//   print('static constexpr double kHighDampOutMax = %r;' % hd['out_max'])
//   print('static constexpr double kHighDampMaxPole = %r;' % d['high_damp_max_pole'])
//   print()
//   names = ['ROOM','HALL','PLATE']
//   for tn in names:
//       T = d['types'][tn]
//       print('// %s' % tn)
//       print('  { \"%s\",' % tn)
//       print('    {%d, %d},' % (T['input_delay'][0], T['input_delay'][1]))
//       print('    {%s},' % ', '.join(str(v) for v in T['combs']))
//       print('    {%s},' % ', '.join(str(v) for v in T['rt_length']))
//       print('    {%s},' % ', '.join('{%d, %d}' % (a,b) for a,b in T['reads']))
//       print('    %r,' % T['input_gain'])
//       ap_l = T['allpass_l']; ap_r = T['allpass_r']
//       print('    {%s},' % ', '.join('{%d, %r}' % (l,u) for l,u in ap_l))
//       print('    {%s},' % ', '.join('{%d, %r}' % (l,u) for l,u in ap_r))
//       print('  },')
//   "
//
// High Damp (2026-09-18 law, docs/rev-highdamp-2026-09-18.md Sec 4): there is
// no per-Type high_damp_map any more. One first-order feedback-path lowpass
// per comb, unity at DC, pole
//     A    = kHighDampOutMax * (HighDamp / kHighDampInMax)   // maps.high_damp
//     A    = min(max(A, 0.0), kHighDampMaxPole)              // clamp, never reached <= 50
//     a_i  = A * rtLength[i] / max_j(rtLength[j])            // per comb, THIS Type's own max
// computed once per Type/HighDamp change (dsp/ax30g_rev.h::recompute), not
// per sample. rtLength[] already carries what "max_j" needs; it is not
// duplicated as a separate constant here.
#pragma once

namespace ax30g {

static constexpr double kRevPreDlySamplesPerMs = 39;
static constexpr double kRevPreDlyOffsetSamples = 2;
static constexpr int kRevBalancePoints = 9;
static constexpr double kRevBalanceX[9] = {0, 5, 10, 15, 20, 25, 35, 45, 50};
static constexpr double kRevBalanceWet[9] = {0.0, 0.0919, 0.1834, 0.2749, 0.4753, 0.7031, 0.8435, 0.9478, 1.0};
static constexpr double kRevBalanceDry[9] = {1.0, 0.9626, 0.9252, 0.8877, 0.8278, 0.7629, 0.468, 0.1558, 0.0};
static constexpr double kRevCombMix = 0.5;
static constexpr double kRevWetPolarity = -1.0;
static constexpr double kRevHighDampInMax = 50.0;
static constexpr double kRevHighDampOutMax = 0.86865;
static constexpr double kRevHighDampMaxPole = 0.995;

static constexpr int kRevNumTypes = 3;
static constexpr int kRevNumCombs = 4;
static constexpr int kRevNumAllpass = 3;

struct RevAllpassSection { int len; double u; };

struct RevTypeTable {
    const char* name;
    int inputDelay[2];             // {L, R}, device samples, added after Pre Dly, at the READ point (Sec 3)
    int combs[4];                  // feedback tap delay d_i, device samples
    int rtLength[4];                // Rev Time law length AND High Damp scaling length
                                     // (== combs[] except PLATE's two short combs, which
                                     // are doubled for both laws -- measured)
    int reads[4][2];                // {L, R} read offset within comb i's own line
    double inputGain;               // wet-path gain (wet polarity is kRevWetPolarity, shared)
    RevAllpassSection allpassL[3];   // {length, u}, in cascade order
    RevAllpassSection allpassR[3];
};

static constexpr RevTypeTable kRevTypes[kRevNumTypes] = {
    // ROOM
    { "ROOM",
      {0, 0},
      {898, 1064, 1687, 1955},
      {898, 1064, 1687, 1955},
      {{0, 814}, {873, 0}, {0, 1150}, {631, 0}},
      0.5254,
      {{151, 0.7544}, {226, 0.6506}, {265, 0.6523}},
      {{130, 0.7126}, {226, 0.6492}, {265, 0.6569}},
    },
    // HALL
    { "HALL",
      {353, 470},
      {4298, 4689, 5002, 5744},
      {4298, 4689, 5002, 5744},
      {{3945, 1641}, {0, 4219}, {4649, 0}, {2149, 5274}},
      0.4541,
      {{809, 0.8007}, {917, 0.6518}, {1955, 0.6058}},
      {{809, 0.8024}, {917, 0.6497}, {1759, 0.5019}},
    },
    // PLATE
    { "PLATE",
      {0, 0},
      {1955, 1994, 4298, 4439},
      {3910, 3988, 4298, 4439},
      {{0, 3908}, {3947, 0}, {0, 2150}, {2228, 0}},
      0.7988,
      {{923, 0.7777}, {1017, 0.701}, {1330, 0.6405}},
      {{1505, 0.7003}, {1017, 0.7}, {1330, 0.6405}},
    },
};

} // namespace ax30g
