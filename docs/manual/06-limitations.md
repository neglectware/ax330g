# 6. Limits of this version

This chapter lists the known differences between the plugin and the unit. It also lists the properties that the project did not measure.

## 6.1 Effects of the unit that are not in the plugin

The unit has 27 effects that connect in chains, and one noise reduction function. The plugin contains 7 of the 27 effects. The project measured the Hyper Resonator, but it is not in the plugin yet.

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

- Programs: the user programs, the preset programs, the program names, and program write and compare.
- The chain rules of the unit. The plugin lets any block go in any slot, in any order. The Mode menu item "As the unit" does not apply these rules yet.
- The pressure pedal and the expression pedal input, and the pedal assignment of parameters.
- The IPE variation library of the unit.
- Total Level, the per-program output level.
- The Individual on/off function of the program switches.
- The tuner.
- The AUX input and the headphone output.
- The Output Level knob of the unit. The Output control of the plugin is a digital gain, not a model of this analog knob.
- The absolute output level of the unit. At LIN, the unit gives 7 dB less level than its input. The plugin gives the same level as its input.

## 6.3 The input stage

- The captures use only the LIN and MAX positions of the Input Level knob. The law of the knob between these positions is not known. The plugin Input control is in dB.
- The model clips a single-sample click about 1 dB more than the unit does. This has no measurable effect on music.
- The even harmonics above the second harmonic are about 5 dB lower in the model than on the unit, at full overload.
- The unit's clip has a knee that is 0.06 dB softer than an ideal hard clip.

## 6.4 The delay blocks

- The Speed control of the plugin moves in steps of 0.01 Hz. The dial of the unit moves in steps of 0.02 Hz up to 0.20 Hz and in steps of 0.1 Hz above that. Thus, the plugin can give Speed values that the unit cannot give.
- The LFO of the plugin starts at the start of its cycle when the host starts or resets the plugin. The LFO of the unit runs freely from power-on.
- Mod Delay: the delay law, feedback, balance and High Damp come from the Stereo Delay. The captures of the Mod Delay use only 200 ms and High Damp 0.
- Stereo Mod Delay: no null test validates the long tails with the LFO on.
- Stereo Delay: the captures of Ducking use only 0, 10, 25, 40 and 50, all at High Damp 0 and at LIN. The model assumes a mono Ducking detector.
- The unit has Ducking also on the Cross Delay, the Tap Tempo Delay and the Hold Delay. These delays are not in the plugin.
- Stereo Delay: the unit has one mono input. The plugin gives the left and right host channels to the two delay lines. With a stereo source, the two lines get different signals.

## 6.5 The 3-Band EQ

- At Mid Freq 250 Hz and 500 Hz, the unit has a level error of up to about 0.8 dB below 150 Hz. The model does not have this error.
- The captures use only 5 of the 13 Mid Freq steps. One law gives the other 8.
- There are no captures of gains between −2 dB and +2 dB. The half-dB steps near 0 dB come from the gain law.
- It is not known if the unit also clips inside each band. The model clips only after the three bands.
- The Mid Freq knob moves in steps of 1 Hz. The block uses the nearest of the 13 steps.

## 6.6 The Reverb

- A long, loud signal makes the reverb path of the unit compress, by up to 9 dB in ROOM and PLATE. The model does not compress.
- The captures use High Damp only at 0, 25 and 50. At High Damp 50, the decay at 1 kHz on the unit is about 4 % different from the model.
- ROOM has the weakest null of the three types, mostly at short Rev Time values.
- The dry column of the Balance table has two measurements that differ by up to 1.5 % at low Balance values.

## 6.7 The Compressor

- The gain law is an extrapolation below −40 dBFS in the measurement files.
- At Sensitivity 50, a floor on the detector limits the maximum gain. This floor is not a measured property. At this value, a short click comes out louder than on the unit.
- The Attack law has three measured points. The Level law has two measured points and the mute at 0. Sensitivity has five measured points.

## 6.8 The Hyper Resonator

The Hyper Resonator is not in the plugin. Its model does not null yet, and the first measurements do not confirm many of its parameters. See [Hyper Resonator](04-blocks/hyper-resonator.md).

## 6.9 The LCD

- Line 1 of the play page is a placeholder, "--- INIT". The unit shows the program number and the program name.
- The LCD shows only the play page. The edit pages of the unit are not in the plugin.

## 6.10 Operating systems

- The plugin is for macOS only.
- Nobody has tested the plugin on macOS 10.13.
