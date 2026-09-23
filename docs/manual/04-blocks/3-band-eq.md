# 3-Band EQ (3BEQ)

## What it does

The 3-Band EQ changes the level of the low, middle and high frequencies. It has a low shelf (Bass), a peak filter with a selectable frequency (Mid), a high shelf (Treble) and a level control (Trim Gain).

The 3-Band EQ has a mono input and a mono output. Both output channels get the same signal. On the unit, it is in Block 1, and it is always the last effect in that block.

A **Stereo In** parameter can run it on each channel separately; see Parameters below.

## Parameters

| Name in the plugin | Name on the unit | Range | Step | Unit | Default |
|---|---|---|---|---|---|
| **Bass** | Bass | −16 to +16 | 0.5 | dB | 0 |
| **Mid Freq** | Mid Freq | 250 to 4000, in 13 steps | see below | Hz | 1000 |
| **Mid Gain** | Mid Gain | −16 to +16 | 0.5 | dB | 0 |
| **Treble** | Treble | −16 to +16 | 0.5 | dB | 0 |
| **Trim Gain** | Trim Gain | −18 to +6 | 0.5 | dB | 0 |
| **Stereo In** | — | Mono, Stereo | — | — | Mono |

- **Bass** sets the level of the low frequencies.
- **Mid Freq** sets the center frequency of the Mid band.
- **Mid Gain** sets the level of the frequencies near Mid Freq.
- **Treble** sets the level of the high frequencies.
- **Trim Gain** sets the level of the block. Use it to prevent overload when you boost a band.
- **Stereo In** selects Mono or Stereo processing. In Stereo, the block runs the same EQ on each channel separately.

**Stereo In** is a list of two items: Mono and Stereo. The box below the knob shows the name of the item. Mono is the default, and it matches the unit.

### The Mid Freq steps

The unit has 13 Mid Freq values. They are the standard one-third-octave frequencies:

250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150 and 4000 Hz.

The Mid Freq control in the plugin is a list of these 13 steps, in the same order as on the unit. The box below the knob shows the name of the step, for example "1000 Hz". Turn the knob to move to the next or the previous step.

## How it works

### The order of the stages

1. Trim Gain, a plain gain.
2. The Bass section.
3. The Mid section.
4. The Treble section.
5. A hard clip.

Claude found that Trim Gain comes before the bands. A +16 dB Treble boost makes the block clip a −20 dBFS sweep. With Trim Gain at −9 dB or −18 dB, the same boost does not clip.

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

Claude measured these phase properties in two independent ways. The model copies them.

### Mid

The Mid section is a peak filter at the Mid Freq step. The section adds or removes a band-pass copy of the signal. The Q of the band-pass filter is different for a boost and for a cut:

| Direction | Q |
|---|---|
| Boost | 0.986 |
| Cut | 2.979 |

Thus, a Mid boost is wide, and a Mid cut of the same size is narrow. A boost and a cut are not mirror images. At 0 dB, the Mid section has no effect.

Claude measured the Mid section at 250, 500, 1000, 2000 and 4000 Hz. The other eight steps come from one law that fits the five measured steps. The law also corrects a small change of bandwidth at 4000 Hz.

### Half-dB steps

The unit changes Bass, Mid Gain, Treble and Trim Gain in steps of 0.5 dB. The plugin uses the same steps. Mark O'Brien captured ±0.5 dB and ±1 dB on each gain, and some half steps at higher gains. Claude compared these captures with the model. The null was −28.3 to −29.5 dB. This is the same as the null of the flat setting, which is the limit of the measurement.

### The hard clip

The block clips symmetrically at 0.93 of the full scale of the converter. The clip is after the three bands.

The block operates on the signal with the pre-emphasis of the input stage still in it. The de-emphasis comes after the block. Thus, treble reaches the clip level sooner than bass. A large Treble boost clips first at high frequencies.

CAUTION: A large boost on more than one band can make the block clip. Decrease Trim Gain to prevent the clip.

### Stereo In

In Stereo, the block runs the whole chain above, with the same settings, on the left and right channels separately. It does not mix them to mono first.

## Measured accuracy

- On 39 of 46 captures, the model frequency response agrees with the unit to 0.043 dB rms from 30 Hz to 18 kHz. The maximum error is 0.177 dB. The phase agrees to 0.15 degrees rms.
- The median null over 47 captures is −28.3 dB overall and −40.7 dB on the steady 1 kHz tone. The bypass capture of the unit nulls at −29.0 dB against the bypass model, on the same test signal.

## Limits

- **Not modeled yet: no law found.** At Mid Freq 250 Hz and 500 Hz, the unit does not come back to 0 dB at low frequencies. The difference is up to about 0.8 dB below 150 Hz. The probable cause is the unit's own coefficient rounding. The model does not have this difference.
- **Not modeled yet: needs captures.** There are no captures of the Mid steps 315, 400, 630, 800, 1250, 1600, 2500 and 3150 Hz. The grid `3beq-4` captures these eight steps, and Mark O'Brien is recording it now.
- **Not modeled yet: no law found.** It is not known if the unit clips inside each band or only after the three bands. The captures agree better with a clip after the bands, but this is not a direct measurement of where the clip sits.

See also [chapter 6](../06-limitations.md).
