# Stereo Delay (SDLY)

## What it does

The Stereo Delay has two delay lines, one for the left channel and one for the right channel. Each channel has its own delay time, feedback and balance. The two channels share one High Damp value and one Ducking value.

On the unit, the Stereo Delay is a member of the Ambience group. The owner's manual calls it "a stereo delay with independent L/R".

## Parameters

| Name in the plugin | Name on the unit | Range | Step | Unit | Default |
|---|---|---|---|---|---|
| **L Dly** | L Dly Time | 5 to 500 | 1 | ms | 300 |
| **R Dly** | R Dly Time | 5 to 500 | 1 | ms | 300 |
| **L Fb** | L Feedback | 0 to 50 | 1 | — | 25 |
| **R Fb** | R Feedback | 0 to 50 | 1 | — | 25 |
| **High Damp** | High Damp | 0 to 50 | 1 | — | 0 |
| **L Bal** | L Balance | 0 to 50 | 1 | — | 25 |
| **R Bal** | R Balance | 0 to 50 | 1 | — | 25 |
| **Ducking** | Ducking | 0 to 50 | 1 | — | 0 |

- **L Dly** and **R Dly** set the delay time of each channel.
- **L Fb** and **R Fb** set the level of the repeats of each channel. A high value gives more repeats.
- **High Damp** sets the loss of high frequencies in the repeats. A high value makes the repeats darker.
- **L Bal** and **R Bal** set the mix of dry signal and delay signal for each channel. At 0 you hear only the dry signal. At 50 you hear only the delay.
- **Ducking** decreases the level of the delay signal when the input level increases. A high value gives a larger decrease. At 0, the Ducking function is off.

## How it works

### Signal flow

Each channel operates as follows:

1. The input sample goes to the dry output.
2. The delay line gives the sample from one delay time before.
3. The feedback gain multiplies the delay output. The result goes back to the input of the delay line with the new input sample.
4. The High Damp filter operates on the signal at the input of the delay line.
5. The delay line keeps the result as a 16-bit sample.
6. The balance tables mix the dry sample and the delay output.
7. The Ducking gain multiplies the delay output only. The dry signal and the feedback loop do not change.

The delay time follows the delay time law in [4.3](README.md#43-the-delay-time-law). The feedback follows the table in [4.5](README.md#45-feedback). The balance follows the table in [4.6](README.md#46-balance). High Damp follows the law in [4.8](README.md#48-high-damp-in-the-delays).

NOTE: The feedback loop gain at Feedback 50 is 0.993. The repeats decrease by about 0.06 dB for each pass. The delay can continue for a long time at this value.

### Ducking

The Ducking function is a gain on the delay output. A detector measures the level of the input signal, and the gain decreases when that level increases. Claude measured the Ducking function from seven captures.

The Ducking law is subtractive and linear:

    gain = 1 − 0.25184 × Ducking × envelope

The gain cannot go below 0. The envelope is the output of the detector.

These properties follow from the law and from the measurements:

- There is no threshold and no knee. A quiet signal also decreases the delay level, by a smaller amount.
- The decrease is in proportion to the input amplitude. An input 20 dB quieter gives one tenth of the gain decrease.
- The decrease is in proportion to the Ducking value.
- At a high Ducking value and a loud input, the gain goes to 0, and the delay signal stops fully.

The table shows the measured decrease of the delay level for a 1 kHz tone into the unit at LIN. The tone levels are relative to the full scale of the measurement files.

| Tone level | Ducking 10 | Ducking 25 | Ducking 40 | Ducking 50 |
|---|---|---|---|---|
| −20 dBFS | −2.4 dB | −7.8 dB | −25.9 dB | −52.0 dB |
| −40 dBFS | −0.2 dB | −0.5 dB | −0.9 dB | −1.1 dB |

The detector has three stages:

1. A frequency weight. Two shelf filters give a slope of about 1.7 dB per octave. Relative to 1 kHz, the detector is 8.5 dB less sensitive at 40 Hz. It is 0.6 dB to 1.0 dB more sensitive from 2 kHz to 12 kHz.
2. A fast peak detector. It follows a new peak in 0.02 ms and falls with a time constant of 1.68 ms.
3. A slow smoother with a time constant of 52 ms, the same for rise and fall.

The peak stage makes the detector react more to broadband material than to a sine wave. Relative to an average detector, the unit decreases the delay level 1.62 times more on white noise and 1.34 times more on a guitar signal.

When the input stops, the delay level comes back to its full value with the 52 ms time constant. The delay repeats that are still in the delay line then continue at their full level. A single short click does not decrease the delay level.

## Measured accuracy

Claude compared the model with captures of the unit at 20 ms, 300 ms and 500 ms delay times. The null depths are in [chapter 5](../05-how-it-was-modelled.md). With the Ducking function on, each capture nulls within 0.5 dB of the same capture without Ducking.

## Limits

- The captures use only the Ducking values 0, 10, 25, 40 and 50. The linear law gives the other values.
- All Ducking captures used High Damp 0 and Input at LIN.
- The feedback value at Feedback 50 comes from a fit with High Damp at 50. There is no measurement of Feedback 46 to 50 on this block without High Damp.
- The unit has one mono input. In the plugin, the two delay lines get the left and right host channels. With a mono source, both lines get the same signal, as on the unit.

See also [chapter 6](../06-limitations.md).
