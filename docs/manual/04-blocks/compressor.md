# Compressor (COMP)

## What it does

The Compressor makes the level of the signal more constant. It increases quiet signals and decreases loud signals. This gives the guitar more sustain.

The Compressor has a mono input and a mono output. Both output channels get the same signal. On the unit, it is in Block 1. The owner's manual says that it makes changes in volume smoother and gives sustain.

A **Stereo In** parameter can apply its gain to each channel separately; see Parameters below.

## Parameters

| Name in the plugin | Name on the unit | Range | Step | Unit | Default |
|---|---|---|---|---|---|
| **Sensitivity** | Sensitivity | 0 to 50 | 1 | — | 40 |
| **Level** | Level | 0 to 50 | 1 | — | 25 |
| **Attack** | Attack | 0 to 50 | 1 | — | 25 |
| **Stereo In** | — | Mono, Stereo | — | — | Mono |

- **Sensitivity** sets how much the block increases quiet signals. A high value gives more sustain.
- **Level** sets the maximum output level of the block.
- **Attack** sets how fast the block decreases the gain at the start of a note. A high value lets more of the start of each note through.
- **Stereo In** selects Mono or Stereo processing. In Stereo, the block applies its gain to each channel's own signal.

**Stereo In** is a list of two items: Mono and Stereo. The box below the knob shows the name of the item. Mono is the default, and it matches the unit.

CAUTION: At Level 0, the block gives no output. This is the behavior of the unit. Set Level above 0 to hear the signal.

## How it works

### The gain law

The compressor of the unit has no threshold, no knee and no ratio control. Its gain follows one law:

    output = input / (a + b × E)

E is the envelope of the input signal (see "The detector" below). The values a and b come from the Sensitivity and Level parameters.

The law has two limits:

- For a very quiet input, E is almost 0, and the gain is 1/a. This is the maximum gain. The block makes quiet signals louder by this gain. This gives sustain.
- For a very loud input, the output goes to the fixed level 1/b. The block acts as a limiter at this level.

Between these limits, the gain changes smoothly. At the top of the measured range, the output changes by only 0.12 dB when the input changes by 12 dB (Sensitivity 50).

### Sensitivity

Sensitivity sets a, and thus the maximum gain. The table gives the measured values:

| Sensitivity | Maximum gain |
|---|---|
| 0 | −0.30 dB |
| 10 | +0.72 dB |
| 25 | +3.19 dB |
| 40 | +9.01 dB |
| 50 | Not measured (see Limits) |

At Sensitivity 0, the block has almost no gain for quiet signals. It only limits loud signals.

The values a and b are not independent. For all Sensitivity values, the gain curves cross at one point. At that input level, the gain is −5.71 dB for all Sensitivity values. Below that level, a high Sensitivity gives more gain. Above that level, a high Sensitivity gives less gain.

### Level

Level sets the output limit 1/b. It does not change the maximum gain much.

| Change | Effect |
|---|---|
| Level 25 to Level 50 | The output limit increases by 5.68 dB. The maximum gain changes by only 0.23 dB. |
| Level 0 | No output (mute) |

A makeup gain after a compressor increases the whole curve. Level does not do this. Level acts more like the target output level of the compressor.

### The detector

The detector follows the peaks of the input signal:

- It rises with a time constant of 1.2 ms.
- It falls with a time constant of 48 ms.

Claude found that the detector measures peaks, not an average. A steady sine gives E equal to its peak value. White noise gives E equal to 4.19 times its rms value. An average detector cannot give both results.

The detector is on the input of the block, not on its output. A detector on the output cannot give the steep limiter curve that the unit has.

### Attack

The Attack parameter does not change the detector. It sets a smoother after the detector. The smoother changes how fast the gain moves at the start of a note, but it does not change the steady gain curve.

| Attack | Smoother time constant |
|---|---|
| 0 | 1.50 ms |
| 25 | 1.75 ms |
| 50 | 11.80 ms |

The block interpolates between these points. Attack has little effect from 0 to 25 and a large effect from 25 to 50.

### Pre-emphasis

The block operates on the signal with the pre-emphasis of the input stage still in it. Thus, the detector reacts a little more to treble than to bass.

### Stereo In

The detector always uses the sum of both channels, in Mono and in Stereo. In Stereo, the same gain goes to each channel's own dry signal, instead of one mono mix. Mark O'Brien chose this design.

## Measured accuracy

The table gives the median null of the model against 9 captures of the unit:

| Test signal | Null |
|---|---|
| Level ramp | −30.3 dB |
| Steady 1 kHz tone, −20 dBFS | −32.5 dB |
| Overall | −21.5 dB |

On the static gain curve, the model agrees with the unit to 0.006 dB to 0.038 dB rms.

## Limits

- **Not modeled yet: needs captures.** The captures measure the gain only for inputs above −40 dBFS in the measurement files. Below this level, the gain law is an extrapolation. The grid `comp-2.json` has rows with the interface send 30 dB down, which moves the same test ramp into this range.
- **Not modeled yet: needs captures.** At Sensitivity 50, the extrapolation gives a very high maximum gain. The model limits it with a floor on the detector value. This floor is not a measured property of the unit; it is the point where the model declines to extrapolate past its own data. The `comp-2.json` rows at Sensitivity 50 below −40 dBFS are aimed at this floor directly.
- **Not modeled yet: needs captures.** At Sensitivity 50, the model is less accurate. The overall null is −16.0 dB, and a short click comes out louder than on the unit. This follows from the same unmeasured floor above.
- **Not modeled yet: needs captures.** The Attack law has only three measured points. The Level law has only two measured points and the mute at 0. The grid `comp-2.json` adds eight Attack points and a second Sensitivity for the Level law.
- **Not modeled yet: needs captures.** The captures use Sensitivity only at 0, 10, 25, 40 and 50.

See also [chapter 6](../06-limitations.md).
