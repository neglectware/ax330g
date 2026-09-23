# 1. Introduction

## 1.1 What AX330G is

AX330G is an audio effect plugin for macOS. It is available in the AU format and in the VST3 format.

The plugin emulates the effects of the Korg Toneworks AX30G and AX300G guitar multi-effects units. Korg released these units in 1997.

The plugin has eight slots. Each slot holds one effect block. The plugin sends the audio through the slots in order, from slot 1 to slot 8.

The plugin also emulates the parts of the unit that every signal goes through:

- The analog input stage and its overload behavior.
- The analog-to-digital and digital-to-analog converters.
- The 16-character, 2-line LCD, with its start-up sequence.

## 1.2 The original unit

The AX30G and the AX300G are floor units for guitar. Each unit has a pressure pedal, footswitches, a dial and a backlit LCD.

The two models have the same signal path, the same effects and the same parameters. The AX300G has more program memory than the AX30G. The measurements in this manual come from an AX300G.

The unit has 28 effect types. Twenty-seven of these effects connect in chains. The twenty-eighth effect is a noise reduction function outside the chain.

The unit has these hardware properties:

| Property | Value |
|---|---|
| Internal sample rate | 39,062.5 Hz |
| Analog-to-digital converter | 18-bit bitstream converter |
| Digital-to-analog converter | 18-bit, 4-times oversampling filter and noise shaper |
| Delay memory | 128 KB |
| Frequency response | 20 Hz to 19 kHz, ±1 dB |
| Guitar input sensitivity | −13 dBu to +8 dBu, 1 MΩ |
| Maximum line output | +5 dBu into 10 kΩ |

## 1.3 What "behavioral emulation" means

The firmware of the unit is in mask ROM. The author did not read, copy or use any code or data from that firmware.

The author measured the unit as a black box. Known test signals went into the unit, and an audio interface recorded the output. The author then made a mathematical model of each effect. Each model gives the same output as the unit for the same input, within measured limits.

Thus, each block in the plugin is a model of behavior that the author measured. It is not a copy of the original program. [Chapter 5](05-how-it-was-modelled.md) tells how the author made and checked the models. [Chapter 6](06-limitations.md) tells where the models are different from the unit.

## 1.4 The name

The name AX330G shows that the plugin is a new product. It is not the AX30G and not the AX300G. The LCD start-up sequence of the plugin shows "AX330G" in the place where the unit shows "AX300G".

## 1.5 What this version contains

| Item | State in version 0.8.6 |
|---|---|
| Effect blocks from the unit | Seven: Stereo Delay, Mod Delay, Stereo Mod Delay, Chorus, 3-Band EQ, Reverb, Compressor |
| Effect blocks that the unit did not have | One: Stereo Chorus |
| Hyper Resonator | Measured, but not in the plugin yet |
| Input stage and converters | In the plugin |
| LCD start-up sequence | In the plugin |
| Programs and presets | Not in the plugin yet |
| The unit's chain rules ("As the unit" mode) | Not in the plugin yet |

## 1.6 Trademarks and independence

AX330G is an independent project by Mark O'Brien, published under the name Neglectware. It is not affiliated with Korg Inc. Korg Inc. does not endorse it.

Korg, Toneworks, AX30G and AX300G are trademarks of their owners. This manual uses these names only to identify the original hardware.
