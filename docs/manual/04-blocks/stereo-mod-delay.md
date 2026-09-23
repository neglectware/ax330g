# Stereo Mod Delay (SMOD)

## What it does

The Stereo Mod Delay has two delay lines, one for the left output and one for the right output. Each line has its own delay time, feedback and balance. One LFO moves both delay times, in opposite directions. This gives a wide stereo sound with a slow change in pitch.

On the unit, the Stereo Mod Delay is a member of the Mod2 group.

## Parameters

| Name in the plugin | Name on the unit | Range | Step | Unit | Default |
|---|---|---|---|---|---|
| **Speed** | Speed | 0.02 to 9.50 | 0.01 | Hz | 1.00 |
| **Depth** | Depth | 0 to 50 | 1 | — | 25 |
| **L Dly Time** | L Dly Time | 1 to 250 | 1 | ms | 100 |
| **R Dly Time** | R Dly Time | 1 to 250 | 1 | ms | 100 |
| **L Fb** | L Feedback | 0 to 50 | 1 | — | 0 |
| **R Fb** | R Feedback | 0 to 50 | 1 | — | 0 |
| **L Bal** | L Balance | 0 to 50 | 1 | — | 50 |
| **R Bal** | R Balance | 0 to 50 | 1 | — | 50 |

- **Speed** sets the rate of the LFO.
- **Depth** sets how far the LFO moves the two delay times.
- **L Dly Time** and **R Dly Time** set the static delay time of each line.
- **L Fb** and **R Fb** set the level of the repeats of each line.
- **L Bal** and **R Bal** set the mix of dry signal and delay signal for each output.

The Stereo Mod Delay has no High Damp parameter, on the unit or in the plugin.

## How it works

1. The block adds the left and right inputs together and divides by 2. Both delay lines get this mono signal.
2. Each delay line has its own feedback loop.
3. One LFO sets the read position of both lines. The left line gets the LFO value, and the right line gets the inverse of the LFO value.
4. The LFO is inside each feedback loop, as in the Mod Delay.
5. Each output mixes the mono dry signal with the output of its own delay line.

Claude measured the opposite movement in the captures. The right-channel delay movement is the left-channel movement with its sign changed, with no time offset between them.

The block uses three shared laws from the [block overview](README.md). These are the delay time law (4.3), the balance table (4.6), and the LFO laws (4.7).

### Feedback

The Stereo Mod Delay has its own feedback table. Claude measured it from long delay tails at Feedback 46 and 50, with more than 100 repeats in each capture.

| Feedback | Loop gain |
|---|---|
| 0 | 0 |
| 10 | 0.2 |
| 25 | 0.5 |
| 40 | 0.8 |
| 46 | 0.9194 |
| 50 | 0.9994 |

The measured points follow the line: loop gain = 0.019987 × Feedback. Thus, the loop gain is linear in the Feedback value. At Feedback 50, it stops just below 1.

NOTE: At Feedback 50, the repeats decrease by only about 0.005 dB for each pass. A delay at this value continues for a very long time. Decrease Feedback or turn the block off to stop it.

### No high-frequency loss in the loop

Claude measured the long tails for a loss of high frequencies in each pass. The measurement found no low-pass filter in the loop with a corner below 40 kHz. Thus, the model has no filter in the loop.

## Measured accuracy

Claude tested the model with Depth 0, delay times of 250 ms, Feedback 46 and Balance 25. The full tail of the model nulls at −41.3 dB against the unit. The tail contains more than 100 repeats. See [chapter 5](../05-how-it-was-modelled.md).

## Limits

- The delay law comes from the Stereo Delay. Claude confirmed it on this block only at 250 ms.
- With the LFO on and high Feedback, Claude did not get a usable null against the unit. Small errors in the LFO rate, depth and shape add up over many repeats. Thus, no null test validates the long modulated tails.
- At Speed 0.6, one measurement gave a real LFO rate of 0.5990 Hz. The model uses 0.5961 Hz. This difference is not confirmed.

See also [chapter 6](../06-limitations.md).
