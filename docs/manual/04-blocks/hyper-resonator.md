# Hyper Resonator (HYPR)

NOTE: **Not in the plugin yet.** This chapter describes the measured behavior of the original unit.

NOTE: Mark O'Brien made a second set of captures. Claude built a new model from them. This chapter describes that model. The C++ port of this block has not started. See Limits, at the end of this chapter, for what is still not settled.

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

    output = Direct × input + Effect × RESONATOR( DRIVER(input), corner(t) )

The block has these parts:

1. The **direct path**, a clean copy of the input. Direct Level sets its level.
2. The **driver**. It adds harmonics to the input. Type and Harmonics control it.
3. The **resonator**. It is a resonant low-pass filter after the driver. Resonance sets its peak.
4. The **detector and trigger**. It watches the level of the input, and fires a sweep when the level crosses a threshold.
5. The **sweep**. Each fire moves the resonator's corner away from its resting frequency, then lets it return. Sensitivity, Depth, Polarity and Decay control it.
6. The **effect path** output. Effect Level sets its level.

The sweep does not follow the input level smoothly. It fires one burst for each rise above the threshold. Every burst has the same rise-and-fall shape, whatever signal fires it. Its peak size depends only on Depth, not on how loud the signal is. This is a triggered sweep, not a smooth follower of the envelope. An earlier version of this chapter described a smooth follower, which the new measurements do not support.

Claude found that the resonator comes after the driver. The harmonics of the clipped signal decrease at 12 dB per octave above the resonator's corner, as a filter after a clip gives.

The whole block operates on the signal with the pre-emphasis of the input stage still in it (see 3.3 in [chapter 3](../03-using-the-plugin.md)). The output of the block does not get the de-emphasis that the other blocks get. Thus, the Hyper Resonator output has a treble lift of up to about +9 dB, relative to 1 kHz. Claude measured this on three different test signals. No other measured block of the unit does this. The reason is still not known.

## The resonator at rest

When the input is quiet, and Polarity is UP, the resonator's corner stays at its resting frequency of **389 Hz**. Three independent measurements gave 388.1 Hz, 388.2 Hz and 388.5 Hz. At Polarity DOWN, the corner rests at a different frequency; see Polarity below.

The resonator is a resonant low-pass filter, not a band-pass filter. Its response is flat below about 150 Hz, has a peak near 389 Hz, and falls steeply above the peak.

The resonator has a second, fixed low-pass filter at 5706 Hz with a Q of 1.92. Claude found this value from the driver harmonics.

The resting frequency is well below 1 kHz. Thus, a steady 1 kHz tone gets almost nothing through the effect path at Type 1 and Harmonics 0. A short click makes the resonator ring, and gets 19 dB more level through the block than through the bypass path.

## Deep dive: each parameter

### Type

Type selects one of two harmonic drivers. Both drivers add a shared even-harmonic part to the input. Each driver also adds its own odd-harmonic part, and Type selects between them.

Type 1's odd-harmonic part is a hard clip. Its gain, and its mix with the even-harmonic part, change with Harmonics; see the table under Harmonics below. On a held-out level ramp, Type 1's harmonics differ from the unit's by 5 dB to 10 dB, rms. This is the least accurate part of the model; see Limits.

Type 2's odd-harmonic part is not a hard clip. It follows the input's own envelope, up to a knee at about −25 dBFS on the model's own internal signal. Below the knee, it rises and falls with the input. Above the knee, it stays at the knee's level. On a held-out level ramp, this law's error is 0.3 dB to 3.3 dB, rms. A hard clip's error there is 4.6 dB to 5.7 dB.

At every measured Harmonics value, the even-harmonic level agrees within about 1 dB between the two Types. For example, both Types read about −67 dB at Harmonics 50. This shows that the two Types share one even-harmonic part.

At Input MAX, the driver still limits the output the same way it does at LIN, 14 dB higher. The trigger that starts each sweep also fires 14 dB lower in the input. Its own threshold sits on the model's internal signal, after the Input control.

### Harmonics

Harmonics sets the gain into the driver, and the mix between the even-harmonic part and the odd-harmonic part, on both Types.

The model uses a table of six measured points: Harmonics 0, 5, 10, 25, 37 and 50.

The even-harmonic level does not rise smoothly with Harmonics. It is loud at Harmonics 10 and 50, and quiet at Harmonics 5 and 25, on both Types. For example, the second harmonic reads about −67 dB at Harmonics 10, and about −87 dB at Harmonics 25.

On a rising level ramp, the even harmonics of both Types pass through deep notches. So do the odd harmonics of Type 1. The notches change with input level, and sit at about the same levels on both Types. See Limits.

### Sensitivity

Sensitivity does not set a gain. It sets the size of a gap. The gap is between the level that fires a sweep, and the level the input must fall back to before the next sweep can fire.

| Sensitivity | Gap |
|---|---|
| 0 | 18.75 dB |
| 10 | 14.5 dB (see Limits) |
| 25 | 9.0 dB (see Limits) |
| 37 | 3.75 dB |
| 50 | 2.25 dB |

A small gap lets the resonator fire a new sweep sooner, after the input falls only a little. On a signal with several notes close together, a small gap fires a new sweep for more of the notes. A large gap can miss a note that starts before the level has fallen far enough.

Sensitivity does not change the size of a sweep, how long it lasts, or the input level that first fires it. Every measured Sensitivity value fires its first sweep at about the same input level.

### Polarity

Polarity sets the resonator's resting frequency, and the direction each sweep moves the corner.

- UP: the corner rests at 389 Hz, the resonator's own resting frequency. A sweep moves the corner up.
- DOWN: the corner rests at about 11.7 kHz, near the top of its range. A sweep moves the corner down, then it returns to 11.7 kHz.

At DOWN, the corner stays near 11.7 kHz between sweeps. This lets more high-frequency content through the resonator than UP does, between sweeps.

An earlier version of this chapter said DOWN rests at 389 Hz and moves down from there. This was wrong. It came from a capture at Type 1 and Harmonics 0, where the resonator was wide open. The measured peak was the guitar's own fundamental tone, not the resonator's corner.

### Depth

Depth sets how far each sweep moves the corner away from its resting frequency.

The corner does not move by an even step in Hz, or in octaves. It moves by an even step of the number that sets the resonator's own filter coefficient:

    F = 2 × sin(π × corner frequency ÷ device rate)

Depth adds a fixed amount to F for each unit of its own value. This amount is 0.038 at UP and 0.026 at DOWN. It stays even to within 3 % across Depth 10, 25, 37 and 50.

The step is even in F, not in Hz. So the same Depth value moves the corner more, in Hz, near 389 Hz than near the top of the range.

At UP and Depth 50, the corner's peak reaches a ceiling of 13.5 kHz. Depth cannot move the corner past this ceiling.

### Decay

Decay sets how long each sweep lasts, before the corner returns to its resting frequency.

| Decay | Time constant |
|---|---|
| 0 | 16.3 ms |
| 10 | 23.5 ms |
| 25 | 52.2 ms |
| 37 | 286 ms |
| 50 | 6.37 s |

The model interpolates these five points on a logarithmic scale.

At Decay 0 and 10, each sweep is so short that the measurement method resolves it poorly; see Limits.

An earlier version of this chapter treated Decay as the release time of a smooth envelope follower. It gave much shorter times, including only 1.5 s at Decay 50. The new measurement replaced that model with the true length of each sweep.

### Resonance

Resonance sets the Q of the resonator. It changes the height of the peak around 389 Hz, and nothing else. Outside about one-third of an octave from the peak, the response stays flat to within ±0.05 dB.

| Resonance | Peak height, relative to Resonance 0 | Q in the model |
|---|---|---|
| 0 | 0 dB | 4.13 |
| 10 | +2.0 dB | 4.94 |
| 25 | +4.8 dB | 7.25 |
| 37 | +9.4 dB | 13.1 |
| 50 | +19.6 dB | 140 |

The model interpolates Q on a curve that is even in 1 ÷ Q, not in Q itself.

At Resonance 50, the peak is narrower than the analysis method can measure directly. The decay rate of a click at Resonance 50 gives a Q near 140 by a separate method. This agrees with the table.

A capture log described the resonator's own ring moving from about 185 Hz at low Resonance to about 385 Hz at high Resonance. This is not a change in the peak's own frequency. At low Resonance, a click's own response from the driver peaks near 185 Hz, and is louder than the resonator's narrow peak. From Resonance 37 up, the resonator's own peak wins instead.

An earlier version of this chapter could not explain a 6.1 dB level increase at Resonance 50 on Type 2 only. The new captures trace this to one capture recorded 6 dB low, not to a real property of Resonance.

### Direct Level

Direct Level sets the level of the clean direct path. The level is linear in the Direct Level value. Direct Level 25 is 5.9 dB below Direct Level 50. A linear law predicts 6.02 dB.

The direct path is a clean copy of the input. Its harmonics stay below −106 dBFS in the measurement files. Its only change is the treble lift of the pre-emphasis, described above.

The direct path and the effect path add in phase. There is no cancellation between them.

### Effect Level

Effect Level sets the level of the effect path. The level is linear in the Effect Level value.

Claude measured this on three different test signals, against Effect Level 50. Effect Level 12, 25 and 37 read 12.3 dB, 6.0 dB and 2.6 dB below it. A linear law predicts 12.4 dB, 6.0 dB and 2.6 dB. The two agree to within 0.1 dB.

Effect Level 0 mutes the effect path.

## Other behavior of the unit

A steady 1 kHz tone gives different harmonics than a 1 kHz tone that rises slowly through the same level. The difference is up to 25 dB, on the unit. This happens with Depth at 0, where the corner does not move. After a tone starts, the output takes about 0.6 s to settle. The model does not have this behavior; see Limits.

An earlier version of this chapter described a corner that moves in steps on a level ramp. It described one jump of 10 dB to 15 dB in 6.7 ms. This is now explained. The jump is a sweep firing, described under The structure of the block, above. It is not a stepped movement of the corner.

## Measured accuracy

The effect path of the Hyper Resonator does not null in the time domain, on either model. The residual has no relation to the capture. A distortion's harmonics do not line up in phase between the model and the unit. The model's driver also switches at moments the real unit does not place identically.

Claude instead measures the corner's own movement, and the harmonic levels, against a held-out guitar signal that no model was fitted to.

| Test | Earlier model | New model |
|---|---|---|
| Corner movement, UP Depth 10 | 582 cents (30 % within 300 cents) | 49 cents (92 %) |
| Corner movement, UP Depth 50 | 2191 cents (12 %) | 71 cents (93 %) |
| Corner movement, DOWN Depth 50 | 5720 cents (0 %) | 179 cents (65 %) |
| Harmonic level, fitted rows | 25.2 dB | 11.0 dB |
| Harmonic level, held-out mixed row | 29.7 dB | 7.8 dB |
| Time-domain null, effect path | 0.0 dB, no relation | 0.0 dB, no relation |

The new model follows the corner's real movement far better than the earlier one, on a signal it never saw during fitting. The harmonic levels also improved. Neither model reproduces the driver's own level-dependent notches; see Limits.

## Limits

- **Not modeled yet: no law found.** On a rising level ramp, the even harmonics of both Types pass through deep notches that change with input level. So do the odd harmonics of Type 1. A simple rectifier or clip cannot make this shape.
- **Not modeled yet: no law found.** Type 1's hard clip gives a curve error of 5 dB to 10 dB, rms, on a held-out level ramp. This is the least accurate part of the model. Type 1's own harmonics at Harmonics 50 also drift by up to 12 dB between different days of captures. This drift is a property of the unit, not of the measurement.
- **Not modeled yet: needs captures.** The gap values at Sensitivity 10 and 25 are bounded, not measured directly. Every value from 5.25 dB to 18.5 dB gives the same result on the current test signals. The model places these two points on a straight line between Sensitivity 0 and Sensitivity 37.
- **Not modeled yet: no law found.** At Sensitivity 37, the model fires one extra sweep on a guitar note. The unit does not fire on that note.
- **Not modeled yet: needs captures.** DOWN's own sweep law rests on captures at one Decay value only. DOWN also renders about 5.5 dB quieter than UP. The grid `hypr-3.json` adds DOWN rows at Decay 0, Decay 50 and Depth 37.
- **Not modeled yet: needs captures.** No capture has tested whether the resonator sits before or after the driver, on a signal that never reaches the driver's clip. The order used in this chapter is an inference from the shape of the harmonics, not a direct test. The grid `hypr-3.json` adds this test.
- **Not modeled yet: needs captures.** A steady tone and a slowly rising tone give different harmonics on the unit (see Other behavior of the unit, above). No capture has held a steady tone long enough to measure how this settles. The grid `hypr-3.json` adds two 30-second tones for this.
- **Not modeled yet: no law found.** Each sweep rises to its peak in about 19 ms, with a time constant of about 8 ms. The measurement method cannot resolve a shorter time than this well.
- **Not modeled yet: no law found.** The reason this block keeps the pre-emphasis treble lift, and does not get de-emphasis like the other blocks, is not known.
- **Not modeled yet: no law found.** The filter's slope above the peak is not settled. The clipped harmonics support 12 dB per octave. Type 1's own, nearly linear response supports a steeper slope instead. The model uses 12 dB per octave.

See also [chapter 6](../06-limitations.md).
