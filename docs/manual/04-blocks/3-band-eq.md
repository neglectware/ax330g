# 3-Band EQ (3BEQ)

## What it does

The 3-Band EQ changes the level of the low, middle and high frequencies. It has a low shelf (Bass), a peak filter with a selectable frequency (Mid), a high shelf (Treble) and a level control (Trim Gain).

The 3-Band EQ has a mono input and a mono output. Both output channels get the same signal. On the unit, it is in Block 1, and it is always the last effect in that block.

## Parameters

| Name in the plugin | Name on the unit | Range | Step | Unit | Default |
|---|---|---|---|---|---|
| **Bass** | Bass | −16 to +16 | 0.5 | dB | 0 |
| **Mid Freq** | Mid Freq | 250 to 4000, in 13 steps | see below | Hz | 1000 |
| **Mid Gain** | Mid Gain | −16 to +16 | 0.5 | dB | 0 |
| **Treble** | Treble | −16 to +16 | 0.5 | dB | 0 |
| **Trim Gain** | Trim Gain | −18 to +6 | 0.5 | dB | 0 |

- **Bass** sets the level of the low frequencies.
- **Mid Freq** sets the center frequency of the Mid band.
- **Mid Gain** sets the level of the frequencies near Mid Freq.
- **Treble** sets the level of the high frequencies.
- **Trim Gain** sets the level of the block. Use it to prevent overload when you boost a band.

### The Mid Freq steps

The unit has 13 Mid Freq values. They are the standard one-third-octave frequencies:

250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150 and 4000 Hz.

The Mid Freq knob in the plugin moves in steps of 1 Hz, and its box shows the knob value without a unit. The block uses the nearest of the 13 steps, on a logarithmic frequency scale:

| Knob value (Hz) | Frequency that the block uses |
|---|---|
| 250 to 280 | 250 Hz |
| 281 to 354 | 315 Hz |
| 355 to 447 | 400 Hz |
| 448 to 561 | 500 Hz |
| 562 to 709 | 630 Hz |
| 710 to 894 | 800 Hz |
| 895 to 1118 | 1000 Hz |
| 1119 to 1414 | 1250 Hz |
| 1415 to 1788 | 1600 Hz |
| 1789 to 2236 | 2000 Hz |
| 2237 to 2806 | 2500 Hz |
| 2807 to 3549 | 3150 Hz |
| 3550 to 4000 | 4000 Hz |

NOTE: To get an exact step, type the step value in the box below the knob.

## How it works

### The order of the stages

1. Trim Gain, a plain gain.
2. The Bass section.
3. The Mid section.
4. The Treble section.
5. A hard clip.

The author found that Trim Gain comes before the bands. A +16 dB Treble boost makes the block clip a −20 dBFS sweep. With Trim Gain at −9 dB or −18 dB, the same boost does not clip.

### Bass and Treble

Each shelf is a first-order section. The section has a fixed filter and one gain value. The fixed filter does not change when you change the gain. Only the gain value changes.

| Section | Corner of the fixed filter |
|---|---|
| Bass | 79.86 Hz |
| Treble | 7997 Hz |

A free shelf fit makes the corner seem to move with the gain. For example, the Bass corner seems to go from 89.6 Hz to 200 Hz when the boost goes from 2 dB to 16 dB. This is only the shape of this type of shelf. The unit does not move the corner.

A boost and a cut of the same size are exact inverses of each other.

The phase response of these two sections is unusual:

- At 0 dB, the Bass section and the Treble section still operate. Each one is a first-order allpass filter. It does not change the level of any frequency, but it changes the phase.
- A boost is not minimum phase. It inverts the phase at its own end of the spectrum.
- A cut is minimum phase.

The author measured these phase properties on the unit in two independent ways. The model copies them.

### Mid

The Mid section is a peak filter at the Mid Freq step. The section adds or removes a band-pass copy of the signal. The Q of the band-pass filter is different for a boost and for a cut:

| Direction | Q |
|---|---|
| Boost | 0.986 |
| Cut | 2.979 |

Thus, a Mid boost is wide, and a Mid cut of the same size is narrow. A boost and a cut are not mirror images. At 0 dB, the Mid section has no effect.

The author measured the Mid section at 250, 500, 1000, 2000 and 4000 Hz. The other eight steps come from one law that fits the five measured steps. The law also corrects a small change of bandwidth at 4000 Hz.

### Half-dB steps

The unit changes Bass, Mid Gain, Treble and Trim Gain in steps of 0.5 dB. The plugin uses the same steps. The gain law of the model gives the values between the measured points.

### The hard clip

The block clips symmetrically at 0.93 of the full scale of the converter. The clip is after the three bands.

The block operates on the signal with the pre-emphasis of the input stage still in it. The de-emphasis comes after the block. Thus, treble reaches the clip level sooner than bass. A large Treble boost clips first at high frequencies.

CAUTION: A large boost on more than one band can make the block clip. Decrease Trim Gain to prevent the clip.

## Measured accuracy

- On 39 of 46 captures, the model frequency response agrees with the unit to 0.043 dB rms from 30 Hz to 18 kHz. The maximum error is 0.177 dB. The phase agrees to 0.15 degrees rms.
- The median null over 47 captures is −28.3 dB overall and −40.7 dB on the steady 1 kHz tone. The bypass capture of the unit nulls at −29.0 dB against the bypass model, on the same test signal.

## Limits

- At Mid Freq 250 Hz and 500 Hz, the unit does not come back to 0 dB at low frequencies. The difference is up to about 0.8 dB below 150 Hz. The model does not have this difference.
- The author did not capture the Mid steps 315, 400, 630, 800, 1250, 1600, 2500 and 3150 Hz.
- The author did not capture gains between −2 dB and +2 dB. The half-dB steps near 0 dB come from the gain law, not from a measurement.
- It is not known if the unit clips inside each band or only after the three bands. The captures agree better with a clip after the bands.

See also [chapter 6](../06-limitations.md).
