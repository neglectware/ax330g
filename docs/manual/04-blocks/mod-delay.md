# Mod Delay (MODD)

## What it does

The Mod Delay is a delay with a pitch change. An LFO moves the delay time up and down, and this gives the delayed signal a slow change in pitch. With a short delay time it gives a chorus sound. With a long delay time it gives a delay with a slow pitch change.

The Mod Delay has one delay line and a mono input. It has two balance controls, one for each output channel. On the unit, the Mod Delay is a member of the Mod2 group. The owner's manual says that it adds changes of pitch to a delayed sound.

A **Stereo In** parameter can give it two delay lines, one for each channel; see Parameters below.

## Parameters

| Name in the plugin | Name on the unit | Range | Step | Unit | Default |
|---|---|---|---|---|---|
| **Dly Time** | Dly Time | 1 to 500 | 1 | ms | 200 |
| **Feedback** | Feedback | 0 to 50 | 1 | — | 0 |
| **High Damp** | High Damp | 0 to 50 | 1 | — | 0 |
| **Speed** | Speed | 0.02 to 9.50 | see [4.7](README.md#47-the-lfo-and-the-speed-parameter) | Hz | 1.00 |
| **Depth** | Depth | 0 to 50 | 1 | — | 25 |
| **L Bal** | L Balance | 0 to 50 | 1 | — | 50 |
| **R Bal** | R Balance | 0 to 50 | 1 | — | 50 |
| **Stereo In** | — | Mono, Stereo | — | — | Mono |

The plugin shows the parameters in the order of this table. The unit shows Speed and Depth first.

- **Dly Time** sets the static delay time. The LFO moves the delay above this value.
- **Feedback** sets the level of the repeats.
- **High Damp** sets the loss of high frequencies in the repeats.
- **Speed** sets the rate of the LFO.
- **Depth** sets how far the LFO moves the delay time.
- **L Bal** and **R Bal** set the mix of dry signal and delay signal for the left output and the right output.
- **Stereo In** selects Mono or Stereo processing. In Stereo, each channel gets its own delay line.

**Stereo In** is a list of two items: Mono and Stereo. The box below the knob shows the name of the item. Mono is the default, and it matches the unit.

## How it works

1. The block adds the left and right inputs together and divides by 2. This gives one mono signal for the delay line.
2. The delay line keeps the mono signal, plus the feedback signal, as 16-bit samples.
3. The LFO sets the read position in the delay line. The delay moves between the Dly Time value and Dly Time plus the peak-to-peak depth.
4. The block reads between two samples with linear interpolation.
5. The feedback gain multiplies the delay output, and the result goes back to the delay line input.
6. The High Damp filter operates on the signal at the delay line input.
7. Each output channel mixes its own dry input signal and the mono delay output, with its own Balance value.

The LFO is inside the feedback loop. Thus, each repeat goes through the delay line again with a new LFO position, and the pitch change increases with each repeat. Claude measured this in captures with short tone bursts. A later repeat of a burst had a larger pitch change than the first repeat.

### Routing

The unit's own routing keeps each channel's dry signal separate; only the wet signal is a mono sum. The plugin follows this routing, as of version 0.8.6.

Claude measured the fix. With a left-only input, and Balance set fully dry, the left output gets 1.00 of the left input, and the right output gets 0.00. Before the fix, each output got 0.50.

### Stereo In

In Stereo, the block uses two delay lines, one for the left channel and one for the right channel. Each line has its own feedback loop. One shared LFO moves both delay times by the same amount, in the same direction. The left line feeds the left output, and the right line feeds the right output.

The block uses the shared laws in the [block overview](README.md):

- Delay time: [4.3](README.md#43-the-delay-time-law).
- Feedback: [4.5](README.md#45-feedback).
- Balance: [4.6](README.md#46-balance).
- Speed, LFO shape and Depth: [4.7](README.md#47-the-lfo-and-the-speed-parameter). At Depth 50, the delay moves by 13.05 ms peak to peak.
- High Damp: [4.8](README.md#48-high-damp-in-the-delays).

### The LFO measurement

Claude measured the LFO of the unit from the pitch of a long tone through the Mod Delay at Feedback 0. The pitch change of the delayed tone gives the rate of change of the delay time. Claude integrated it to get the delay against time.

At Speed 1, 5 and 9.5, the model delay follows the measured delay to within 0.08 to 0.22 device samples rms. One device sample is 0.0256 ms.

## Limits

- **Not modeled yet: needs captures.** The captures of the Mod Delay use only Dly Time 200 ms, at Feedback 0 and 25, and at High Damp 0.
- **Not modeled yet: needs captures.** The delay law, the feedback table, the balance table and the High Damp law come from the Stereo Delay measurements. The project did not measure them separately on the Mod Delay. Captures of the Mod Delay itself, at more Dly Time and High Damp values, would test whether it shares these tables.

See also [chapter 6](../06-limitations.md).
