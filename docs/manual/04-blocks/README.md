# 4. The effect blocks

This chapter has one section for each block in the plugin. It also has one section for the Hyper Resonator, which is not in the plugin yet. This page gives the properties that more than one block uses.

## 4.1 The blocks

| Block | LCD | Group on the unit | On the unit? | Chapter |
|---|---|---|---|---|
| Stereo Delay | SDLY | Ambience | Yes | [Stereo Delay](stereo-delay.md) |
| Mod Delay | MODD | Mod2 | Yes | [Mod Delay](mod-delay.md) |
| Stereo Mod Delay | SMOD | Mod2 | Yes | [Stereo Mod Delay](stereo-mod-delay.md) |
| Chorus | CHO | Mod1 | Yes | [Chorus](chorus.md) |
| Stereo Chorus | SCHO | — | **No** | [Stereo Chorus](stereo-chorus.md) |
| 3-Band EQ | 3BEQ | Block 1 | Yes | [3-Band EQ](3-band-eq.md) |
| Reverb | REV | Ambience | Yes | [Reverb](reverb.md) |
| Compressor | COMP | Block 1 | Yes | [Compressor](compressor.md) |
| Hyper Resonator | HYPR | Block 1 | Yes, not in the plugin yet | [Hyper Resonator](hyper-resonator.md) |

## 4.2 Parameter values

Each parameter uses the same range as the unit. Most parameters are integers from 0 to 50, as on the unit. The owner's manual of the unit gives the ranges but not the steps. The steps in this manual come from the plugin code and from measurements of the unit.

The default value of each parameter is the value that the block gets when you select it in a slot.

## 4.3 The delay time law

Stereo Delay, Mod Delay, Stereo Mod Delay and the Pre Dly parameter of the Reverb use one law for their delay time. The author measured this law on the unit:

    delay in device samples = round(39 × displayed ms) + 2

The device rate is 39,062.5 Hz. Thus, 1 displayed millisecond is 0.9984 ms, and the offset of 2 samples adds 0.051 ms.

| Displayed | Device samples | Real delay |
|---|---|---|
| 1 ms | 41 | 1.050 ms |
| 20 ms | 782 | 20.019 ms |
| 100 ms | 3,902 | 99.891 ms |
| 250 ms | 9,752 | 249.651 ms |
| 300 ms | 11,702 | 299.571 ms |
| 500 ms | 19,502 | 499.251 ms |

The Chorus and the Stereo Chorus have no delay time parameter. They use a fixed delay (see their chapters).

## 4.4 The delay memory and the converters

The unit keeps delayed audio as 16-bit linear samples at the full device rate. The delay blocks in the plugin do the same. The unit has 128 KB of delay memory, which holds more than 1 s of 16-bit audio at the device rate.

At its boundaries, each block rounds the signal to the 18-bit word length of the unit's converters. The feedback multiplication in the delay blocks keeps 16 bits and removes the rest (truncation), as the unit does.

## 4.5 Feedback

The **Feedback** parameters (Feedback, L Fb, R Fb) set the gain of the signal that goes from the delay output back to the delay input. Each repeat is quieter than the one before by this gain.

The Stereo Delay and the Mod Delay use this table. The plugin interpolates in a straight line between the points.

| Feedback | Loop gain |
|---|---|
| 0 | 0 |
| 10 | 0.2 |
| 25 | 0.5 |
| 40 | 0.8 |
| 50 | 0.993 |

The Stereo Mod Delay has its own table (see [Stereo Mod Delay](stereo-mod-delay.md)). The author measured it at high Feedback values.

## 4.6 Balance

The **Balance** parameters (L Bal, R Bal, Balance) set the mix of the dry signal and the effect signal. Balance 0 gives the dry signal only. Balance 50 gives the effect signal only.

The unit does not use a linear crossfade or an equal-power crossfade. It uses two separate tables, one for the dry gain and one for the effect (wet) gain. The delay blocks use this measured table:

| Balance | Wet gain | Dry gain |
|---|---|---|
| 0 | 0.000 | 1.000 |
| 5 | 0.092 | 0.963 |
| 10 | 0.184 | 0.925 |
| 15 | 0.275 | 0.888 |
| 20 | 0.476 | 0.828 |
| 25 | 0.705 | 0.763 |
| 30 | 0.792 | 0.625 |
| 35 | 0.844 | 0.469 |
| 40 | 0.896 | 0.312 |
| 45 | 0.948 | 0.157 |
| 50 | 1.000 | 0.000 |

The wet gain increases slowly from 0 to 15, then quickly from 15 to 25. At Balance 25, both signals are about 2 dB to 3 dB below full level. The Reverb uses its own table (see [Reverb](reverb.md)).

## 4.7 The LFO and the Speed parameter

Mod Delay, Stereo Mod Delay, Chorus and Stereo Chorus use one low-frequency oscillator (LFO) design. The LFO moves the read position of a delay line, and this changes the pitch of the delayed signal.

### Speed

The **Speed** parameter sets the LFO rate.

| Property | Value |
|---|---|
| Range | 0.02 Hz to 9.50 Hz |
| Step in the plugin | 0.01 Hz |
| Steps of the unit's dial | 0.02 Hz from 0.02 to 0.20 Hz, then 0.1 Hz from 0.2 to 9.5 Hz |
| Default | 1.00 Hz |

The real rate is a little lower than the displayed rate. The author measured this table on the unit:

| Displayed Speed | Real rate |
|---|---|
| 0.02 Hz | 0.0186 Hz |
| 0.10 Hz | 0.0978 Hz |
| 0.20 Hz | 0.1979 Hz |
| 0.50 Hz | 0.4960 Hz |
| 1.00 Hz | 0.9965 Hz |
| 2.00 Hz | 1.9931 Hz |
| 5.00 Hz | 4.9897 Hz |
| 9.50 Hz | 9.4812 Hz |

### Shape

The LFO shape is a measured table of 256 points for one cycle. The shape is between a sine and a triangle. Relative to the fundamental, its third harmonic is at −24.9 dB and its fifth harmonic is at −34.4 dB. A pure triangle has these harmonics at −19.1 dB and −27.9 dB. A pure sine has neither.

### Depth

The **Depth** parameter sets how far the LFO moves the delay. The movement is linear in Depth:

| Depth | Peak-to-peak delay change |
|---|---|
| 1 | 0.261 ms |
| 25 | 6.52 ms |
| 50 | 13.05 ms |

The delay moves only above its static value. At the lowest point of the LFO cycle, the delay is equal to the static delay. At the highest point, it is the static delay plus the peak-to-peak value.

### Phase

The LFO of the unit runs freely from power-on. The LFO of the plugin starts at the start of its cycle when the host starts or resets the plugin. Thus, the LFO phase at a given time is not the same as on the unit.

## 4.8 High Damp in the delays

The **High Damp** parameter of the Stereo Delay and the Mod Delay adds a one-pole low-pass filter at the input of the delay line. The filter is inside the feedback loop. Thus, each repeat goes through the filter again and becomes darker than the repeat before it.

The filter pole is 0.0195 × High Damp. The table gives the −3 dB frequency of one pass through the filter, from the plugin code:

| High Damp | −3 dB frequency, one pass |
|---|---|
| 0 | No filter |
| 1 to 9 | Above 14 kHz, or no −3 dB point below the Nyquist frequency |
| 10 | 14.3 kHz |
| 15 | 8.9 kHz |
| 20 | 6.3 kHz |
| 25 | 4.7 kHz |
| 30 | 3.4 kHz |
| 35 | 2.4 kHz |
| 40 | 1.6 kHz |
| 45 | 814 Hz |
| 50 | 157 Hz |

The Reverb has a different High Damp law (see [Reverb](reverb.md)).
