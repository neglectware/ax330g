# 6. Limits of this version

This chapter lists the known differences between the plugin and the unit. Each difference carries one of four labels.

## 6.0 The four labels

- **Not modeled yet: needs captures.** The project has not measured this part of the unit yet. This chapter names the capture that would settle it.
- **Not modeled yet: no law found.** The project measured this part of the unit. Claude has not found a model that predicts the measurement.
- **Deliberate difference.** The plugin does this on purpose. This chapter gives the reason.
- **Not in the plugin yet.** The unit has this feature. The plugin does not have it yet.

## 6.1 Effects of the unit that are not in the plugin

**Not in the plugin yet**, for every row below. The unit has 27 effects that connect in chains, and one noise reduction function. The plugin contains 7 of the 27 effects. The project measured the Hyper Resonator, but it is not in the plugin yet.

| Group on the unit | Effect | In the plugin |
|---|---|---|
| Block 1 | COMP, Compressor | Yes |
| Block 1 | DST1, Distortion 1 | No |
| Block 1 | DST2, Distortion 2 | No |
| Block 1 | WAH, Wah | No |
| Block 1 | 3BEQ, 3-Band Equalizer | Yes |
| Block 1 | HYPR, Hyper Resonator | No. Measured, not in the plugin yet. |
| A.Sim/Exct | ASIM, Amp Simulator | No |
| A.Sim/Exct | EXCT, Exciter | No |
| Mod1 | CHO, Chorus | Yes |
| Mod1 | FLAN, Flanger | No |
| Mod1 | PHAS, Phaser | No |
| Mod1 | VIBR, Vibrato | No |
| Mod1 | TRML, Tremolo | No |
| Mod1 | RING, Ring Modulator | No |
| Mod2 | MODD, Modulation Delay | Yes |
| Mod2 | SMOD, Stereo Modulation Delay | Yes |
| Mod2 | SWPM, Sweep Modulation Delay | No |
| Mod2 | SPHS, Stereo Phaser | No |
| Mod2 | RNDF, Random Step Filter | No |
| Mod2 | PTCH, Pitch Shifter | No |
| Mod2 | BEND, Pedal Bender | No |
| Mod2 | PAN, Panner | No |
| Ambience | SDLY, Stereo Delay | Yes |
| Ambience | XDLY, Cross Delay | No |
| Ambience | TDLY, Tap Tempo Delay | No |
| Ambience | HDLY, Hold Delay | No |
| Ambience | REV, Reverb | Yes |
| Outside the chain | NR, Noise Reduction | No |

NOTE: The Distortion 1 of the unit uses an analog diode clip circuit outside the digital signal processor. It will need its own measurement method.

## 6.2 Functions of the unit that are not in the plugin

**Not in the plugin yet**, for every item below.

- The unit's own programs. The plugin has presets in banks (see [3.10](03-using-the-plugin.md#310-presets)), but the preset programs and user programs of the unit are not in the plugin yet. The plugin has no factory banks in this version.
- Program compare. The plugin marks a changed preset, but it cannot compare the changed values with the saved values.
- The rules of the unit for program names: a maximum of 10 characters from the character set of the display. The plugin accepts longer names and other characters. The LCD shows the first characters only, and shows "?" for a character that its font does not have.
- The chain rules of the unit. The plugin lets any block go in any slot, in any order. Setting Open mode off ("as the unit") does not apply these rules yet.
- The pressure pedal and the expression pedal input, and the pedal assignment of parameters. A preset does not store a pedal assignment.
- The IPE variation library of the unit.
- Total Level, the per-program output level. A preset does not store it.
- The Individual on/off function of the program switches.
- The tuner.
- The AUX input and the headphone output.

### The Output control

**Deliberate difference.** The plugin's Output control is a calibrated digital gain, not a model of the unit's analog Output Level knob. With Input and Output at 0 dB, the plugin passes a 1 kHz signal at the same level it went in. The real unit gives 7 dB less level than its input at LIN.

Claude chose this so that a host's own gain staging stays predictable. The 0 dB point matches the same 1 kHz reference used to measure every block model, not the unit's own insertion loss. The Output knob's own analog law was never captured, so a true model of it does not exist yet.

## 6.3 The input stage

- **Not modeled yet: needs captures.** The captures use only the LIN and MAX positions of the Input Level knob. The law of the knob between these two positions is not known. A few captures at knob positions between LIN and MAX would give this law. Until then, the plugin shows the Input control in dB, not in the numbers of the unit's knob.
- **Not modeled yet: no law found.** The model clips a single-sample click about 1 dB more than the unit does. The likely cause is the order of two steps. The model limits its bandwidth first and then applies the treble-boost filter.
- The unit applies that filter first, across its full analog bandwidth, and limits the bandwidth after. Neither of the two fixes Claude tried removed the difference. It has no measurable effect on music.
- **Not modeled yet: no law found.** The even harmonics above the second harmonic are about 5 dB lower in the model than on the unit, at full overload. One fixed DC offset in the clipper gives the model's second harmonic the right level. But the model's higher even harmonics then fall off faster than the unit's do. Whatever the unit does to make its even harmonics is more than one fixed offset.
- **Not modeled yet: no law found.** The unit's clip has a knee that is 0.06 dB softer than an ideal hard clip. No softer clip shape has been fitted to this knee yet.

## 6.4 The delay blocks

Version 0.8.6 fixed the routing of the Mod Delay, the Reverb and the Stereo Mod Delay to match the unit. See [chapter 3](03-using-the-plugin.md#mono-and-stereo-blocks) and each block's own chapter.

- **Deliberate difference.** The LFO of the plugin starts at the start of its cycle when the host starts or resets the plugin. The LFO of the unit runs freely from power-on. The capture method never reset the unit's LFO, so its phase at any moment is an accident of when someone last plugged it in. The plugin cannot know or copy that accident. A fixed start gives the same result every time you load a project.
- **Not modeled yet: needs captures.** Mod Delay: the delay law, feedback, balance and High Damp come from the Stereo Delay measurements, not from captures of the Mod Delay itself. The captures of the Mod Delay use only 200 ms and High Damp 0. Captures of the Mod Delay at more Dly Time and High Damp values would test whether it shares these tables with the Stereo Delay.
- **Not modeled yet: no law found.** Stereo Mod Delay: no null test validates the long tails with the LFO on. Small errors in the LFO rate, depth and shape add up over many repeats. Claude has not found a fit that nulls a long modulated tail.
- **Not modeled yet: needs captures.** Stereo Delay: the captures of Ducking use only 0, 10, 25, 40 and 50, all at High Damp 0 and at LIN. The model assumes a mono Ducking detector. The grid `sdly-ducking-2.json` has rows for Ducking 5, 20, 30 and 45. It also has rows for the detector's response with High Damp and Input engaged. It settles both open points once captured.
- **Not modeled yet: needs captures.** Stereo Delay: the feedback value at Feedback 50 comes from a fit made with High Damp at 50. There is no measurement of Feedback 46 to 50 on this block without High Damp. The grid `sdly-tail.json` is a long feedback tail at High Damp 0 that would give this value directly.
- **Not in the plugin yet.** The unit has Ducking also on the Cross Delay, the Tap Tempo Delay and the Hold Delay. These three delays are not in the plugin.

## 6.5 The 3-Band EQ

- **Not modeled yet: no law found.** At Mid Freq 250 Hz and 500 Hz, the unit has a level error of up to about 0.8 dB below 150 Hz. The probable cause is the unit's own coefficient rounding. The new captures at 315 Hz and 400 Hz may allow a table for this error.
- **Not modeled yet: needs captures.** The captures use only 5 of the 13 Mid Freq steps: 250, 500, 1000, 2000 and 4000 Hz. One law gives the other 8 steps. The grid `3beq-4` captures those eight missing steps, and Mark O'Brien is recording it now.
- **Not modeled yet: no law found.** It is not known if the unit also clips inside each band. The model clips only after the three bands. The captures agree better with a clip placed after the bands, but this is not a direct measurement of where the clip sits.

## 6.6 The Reverb

- **Not modeled yet: no law found.** A long, loud signal makes the reverb path of the unit compress, by up to 9 dB in ROOM and PLATE. The likely cause is the wet path reaching a ceiling after its per-type gain stage; a click carries too little energy to reach it. The compression is measured at one signal level only, so no full curve has been fitted. The model does not compress.
- **Not modeled yet: needs captures.** The captures use High Damp only at 0, 25 and 50. The model is linear between these values. Captures at more High Damp values would test this line.
- **Not modeled yet: no law found.** At High Damp 50, the decay at 1 kHz on the unit is about 4 % slower than the model's own decay. Making one comb's per-pass loss smaller would remove the drift, but it would also make other captures worse. The cause of the drift is not settled.
- **Not modeled yet: no law found.** ROOM has the weakest null of the three types, mostly at short Rev Time values. ROOM's comb filters are the shortest of the three types, so the sound goes around its loops about 40 times a second. A small error in any one comb compounds fastest there.
- **Not modeled yet: needs captures.** There are no captures at Balance 30 and 40. The model interpolates these values. A capture at each of these two settings, on any type, would settle them.
- **Not modeled yet: no law found.** The dry column of the Balance table has two measurements that differ by up to 1.5 % at low Balance values. A second measurement of the same rows did not resolve which value is right.

## 6.7 The Compressor

- **Not modeled yet: needs captures.** The gain law is an extrapolation below −40 dBFS in the measurement files. The grid `comp-2.json` has rows with the interface send 30 dB down. This moves the same test ramp into that range. Capturing and fitting them will settle the law there.
- **Not modeled yet: needs captures.** At Sensitivity 50, a floor on the detector limits the maximum gain. This floor is not a measured property; it is the point past which the model declines to extrapolate. At this value, a short click comes out louder than on the unit. The `comp-2.json` rows at Sensitivity 50 below −40 dBFS are aimed at this floor directly.
- **Not modeled yet: needs captures.** The Attack law has three measured points, at Attack 0, 25 and 50. The Level law has two measured points, at Level 25 and 50, and the mute at 0. Sensitivity has five measured points. The grid `comp-2.json` adds eight Attack points and a second Sensitivity for the Level law.

## 6.8 The Hyper Resonator

**Not in the plugin yet.** Its effect path does not null in the time domain. The `hypr-2.json` grid gave a new model, with much better corner tracking and harmonic levels than the first model. Several driver and sweep properties are still not settled. The grid `hypr-3.json` asks for further captures on these. See [Hyper Resonator](04-blocks/hyper-resonator.md).

## 6.9 The LCD

- **Deliberate difference.** Line 1 of the play page shows a bank letter and a preset number with three digits, for example "A012". The unit uses a bank letter and two digits, for example "A11" or "U11", or "P" and a number for its preset programs. The plugin uses one more digit, for up to 999 presets in a bank, and up to 26 banks.
- **Deliberate difference.** Line 1 shows "*" at its end when the preset has changes. The manuals of the unit do not show such a mark in the play page.
- **Not in the plugin yet.** The LCD shows only the play page. The edit pages of the unit are not in the plugin.

## 6.10 Operating systems

These two items are about the platform, not about a difference from the unit.

- The Windows version has had one test, of version 0.8.6. See [2.6](02-installation.md#26-windows).
- Nobody has tested the plugin on macOS 10.13.
