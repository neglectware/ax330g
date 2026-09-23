# Hyper Resonator (HYPR)

NOTE: **Not in the plugin yet.** This chapter describes the measured behavior of the original unit.

NOTE: Mark O'Brien made a second set of captures. Claude will revise this chapter when the new model is ready. The first set of measurements could not settle some properties. This chapter marks each of these properties as "not confirmed".

## What it does

The Hyper Resonator is a distortion followed by a resonant filter that moves with the level of the input. The owner's manual says that it combines a harmonic driver and a sweep resonator. It says that the sounds go from fuzz sounds to guitar synthesizer sounds.

On the unit, the Hyper Resonator is in Block 1. When a program uses it, it is the only effect in Block 1. On the unit, you can control Harmonics, Depth or Resonance with the pressure pedal.

## Parameters of the unit

| Name on the unit | Range | Description in the owner's manual (paraphrase) |
|---|---|---|
| **Type** | 1, 2 | Selects the type of distortion. 1 is a fuzz. 2 is a distortion with more overtones. |
| **Harmonics** | 0 to 50 | Sets the quantity of overtones. |
| **Sensitivity** | 0 to 50 | Sets the sensitivity of the resonator. |
| **Polarity** | UP, DOWN | Sets the direction of the resonator sweep. |
| **Depth** | 0 to 50 | Sets the depth of the resonator sweep. |
| **Decay** | 0 to 50 | Sets the time of the resonator sweep. |
| **Resonance** | 0 to 50 | Sets the quantity of resonance. |
| **Direct Level** | 0 to 50 | Sets the level of the dry signal. |
| **Effect Level** | 0 to 50 | Sets the level of the effect signal. |

## The structure of the block

The measurements give this structure:

    output = Direct × input + Effect × RESONATOR( DRIVER(input), corner(E) )

The block has these parts:

1. The **direct path**, a clean copy of the input. Direct Level sets its level.
2. The **driver**. It adds harmonics to the input. Type and Harmonics control it.
3. The **resonator**. It is a resonant low-pass filter after the driver. Resonance sets its peak.
4. The **envelope detector**. It measures the level of the input (E).
5. The **sweep law**. It moves the corner frequency of the resonator with the envelope. Sensitivity, Depth, Polarity and Decay control it.
6. The **effect path** output. Effect Level sets its level.

Claude found that the resonator comes after the driver. The harmonics of the clipped signal decrease at 12 dB per octave above the resonator corner, as a filter after a clip gives. Also, on Type 2, the Resonance value changes the level of the clipped signal. A filter before the clip cannot do this.

The whole block operates on the signal with the pre-emphasis of the input stage still in it (see 3.3 in [chapter 3](../03-using-the-plugin.md)). The output of the block does not get the de-emphasis that the other blocks get. Thus, the Hyper Resonator output has a treble lift of up to about +9 dB, relative to 1 kHz. Claude measured this on three different test signals. No other measured block of the unit does this. The reason is not known.

## The resonator at rest

When the input is quiet, the resonator stays at its rest frequency of **389 Hz**. Three independent measurements gave 388.1 Hz, 388.2 Hz and 388.5 Hz.

The resonator is a resonant low-pass filter, not a band-pass filter. Its response is flat below about 150 Hz, has a peak near 389 Hz, and falls steeply above the peak.

The resonator has a second, fixed low-pass filter at 5706 Hz with a Q of 1.92. Claude found this value from the driver harmonics.

The rest frequency is well below 1 kHz. Thus, a steady 1 kHz tone gets almost nothing through the effect path at Type 1 and Harmonics 0. A short click makes the resonator ring, and gets 19 dB more level through the block than through the bypass path.

## Deep dive: each parameter

### Type

Type selects one of two drivers. The measurements at Harmonics 0, 25 and 50 give this behavior:

| Harmonics | Type 1 | Type 2 |
|---|---|---|
| 0 | Almost linear, and about 50 dB quieter than the input. No measurable harmonics. | A hard clip. Odd harmonics only. The even harmonics are 50 dB to 70 dB below. |
| 25 | Mostly odd harmonics | Odd harmonics, with a small quantity of even harmonics |
| 50 | Almost only even harmonics. The second harmonic is 19.4 dB above the fundamental. | Odd and even harmonics. The fundamental stays. |

Thus, Type 1 goes from linear, to odd harmonics, to even harmonics as Harmonics increases. Type 1 at Harmonics 50 acts as a frequency doubler, like a full-wave rectifier. It does not act as a fuzz at that value.

Type 2 goes from odd harmonics to odd and even harmonics. At Harmonics 50, both types have the same second harmonic level, within 0.1 dB. This shows that the even harmonics come from one part that both types share.

The Type 2 driver ends in a hard clip. The output of a 1 kHz tone increases 1:1 with the input up to −23.6 dBFS in the measurement files. Above this level, the output stays flat within ±0.1 dB for the next 22 dB. The clip level is −25.6 dBFS in the measurement files, which is −23.5 dBFS at the converter.

At Input MAX, the driver still limits the output. The extra 14 dB of input gives 14 dB more output at low levels and no more output at high levels.

### Harmonics

Harmonics sets the gain into the driver and the mix of odd and even harmonics. See the Type table above.

The model uses a table of driver gain and odd/even mix at Harmonics 0, 25 and 50 for each Type. The fit error over 42 harmonic levels is 6.25 dB rms. This is not a good fit.

Not confirmed: The form of the driver between the measured points. Three points cannot show if Harmonics crossfades between two shapes, moves a bias, or increases the gain into one fixed shape.

Not confirmed: How the unit removes the fundamental at Type 1, Harmonics 50. The unit gives the fundamental 21 dB below the model.

### Sensitivity

Sensitivity sets how fast the resonator corner moves when the input level changes.

The measurements on a guitar signal give this behavior:

- Below an input envelope of about −34 dBFS in the measurement files, the corner does not move. It stays at the rest frequency. This level is about −36 dBFS at the converter.
- Above this threshold, the corner moves by about 0.8 octave for each dB at Sensitivity 50. At Sensitivity 25, it moves by about 0.55 octave for each dB.
- The movement stops at about 4.6 octaves above the rest frequency, near 9 kHz.

The model uses a sensitivity factor of 1.00 at Sensitivity 50 and 0.69 at Sensitivity 25. Claude measured these two points.

Not confirmed: The factor 0.35 at Sensitivity 10. This value is an estimate.

Not confirmed: The effect of Sensitivity in general. On the test signals with the quietest driver, Sensitivity 10, 25 and 50 gave the same result. On the guitar signal with a loud driver, Sensitivity 25 and 50 gave different results. The new measurements will settle this.

Not confirmed: The detector input. The model takes the envelope from the input of the block. It is possible that the unit takes it from the driver output.

### Polarity

Polarity sets the direction of the corner movement:

- UP: the corner moves up from 389 Hz when the input level increases.
- DOWN: the corner moves down from 389 Hz when the input level increases.

On one DOWN capture, the resonator peak was at 188 Hz to 234 Hz. On the same test with UP, it was at 375 Hz and above.

Not confirmed: The range of the movement at DOWN. There is only one DOWN capture, at the quietest driver value.

### Depth

Depth sets the size of the corner movement. The model uses a linear law. Depth 0 gives no movement, Depth 25 gives half of the full movement, and Depth 50 gives the full movement.

Not confirmed: The Depth law. The first captures do not measure it. On the test signals with the quietest driver, Depth 25 and Depth 50 gave almost the same result.

### Decay

Decay sets how slowly the envelope falls after the input level decreases. Thus, it sets how slowly the corner returns to its rest frequency.

| Decay | Release time constant in the model |
|---|---|
| 0 | 60 ms |
| 25 | 200 ms |
| 50 | 1500 ms |

These facts support the table:

- On a level ramp, the resonator corner passed 1 kHz 98 ms later at Decay 25 than at Decay 0.
- At Decay 50, the resonator was still open 1.5 s after the previous test signal stopped. This agrees with a release of about 1 s or more.

Not confirmed: The whole Decay law. It is the weakest law in the model.

Not confirmed: The attack of the detector. The model uses 1.2 ms, the value of the Compressor detector.

### Resonance

Resonance sets the Q of the resonator. It changes the height of the peak and nothing else. Outside one-third of an octave around 389 Hz, the response stays within ±0.5 dB from 40 Hz to 6 kHz.

| Resonance | Peak relative to Resonance 0 | Q in the model |
|---|---|---|
| 0 | 0 dB | 4.28 |
| 25 | +5.6 dB | 8.2 |
| 50 | +23.5 dB | 64 |

The Q at Resonance 50 is a minimum value. The real peak is narrower than the measurement bands, so the real Q can be higher.

Not confirmed: Why Resonance 50 increases the whole effect output by 6.1 dB on Type 2, but not on Type 1. A filter after a saturated clip should not do this.

Not confirmed: The slope of the resonator above the peak. The clipped harmonics give 12 dB per octave. The almost linear Type 1 response gives about 24 dB per octave. The model uses 12 dB per octave.

### Direct Level

Direct Level sets the level of the clean direct path. The level is linear in the Direct Level value. Direct Level 25 is 5.9 dB below Direct Level 50. A linear law predicts 6.02 dB.

The direct path is a clean copy of the input. Its harmonics stay below −106 dBFS in the measurement files. Its only change is the treble lift of the pre-emphasis, as described above.

The direct path and the effect path add in phase. There is no cancellation between them.

### Effect Level

Effect Level sets the level of the effect path.

Not confirmed: The Effect Level law. The first measurements did not isolate it. The model uses a linear law, as for Direct Level.

## Other behavior of the unit

Not confirmed: A slow internal state. A steady 1 kHz tone and a 1 kHz tone that increases slowly through the same level give different harmonics, by up to 25 dB. This occurs with Depth at 0, where the corner does not move.

At Type 1 and Harmonics 50, the fundamental and the second harmonic change places. After a tone starts, the output takes about 0.6 s to become steady. The model does not have this behavior.

Not confirmed: The corner moves in steps. On a level ramp, the output made one jump of 10 dB to 15 dB in 6.7 ms. A corner that changes smoothly cannot do this. The size of the steps is not known, and the model moves the corner smoothly.

## Measured accuracy of the current model

The Hyper Resonator is the first block of the unit that is both nonlinear and time-variant. A null test does not work on its effect path:

- The effect path nulls at 0.0 dB. The model output and the capture have no correlation.
- The direct path nulls at −26.8 dB on the level ramp.

For this reason, Claude uses other measures:

| Measure | Result |
|---|---|
| Static curve of the driver on Type 2, with Depth 0 | 0.8 dB to 2.8 dB rms error over 40 dB of input |
| Harmonic levels | 12.2 dB rms median error |
| Corner frequency movement | 575 cents rms median error |

The static curve agrees well. The harmonic levels and the corner movement do not. The first measurements had all sweep tests at the quietest driver value, where the effect path is 50 dB down. The new measurements repeat these tests with a loud driver.
