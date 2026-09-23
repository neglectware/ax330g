# Chorus (CHO)

## What it does

The Chorus adds one copy of the signal, with a short delay that an LFO moves. The movement of the delay gives the copy a small, slow change in pitch. The copy and the dry signal together give the chorus sound.

The Chorus has a mono input and a mono output. Both output channels get the same signal. On the unit, the Chorus is the first member of the Mod1 group.

## Parameters

| Name in the plugin | Name on the unit | Range | Step | Unit | Default |
|---|---|---|---|---|---|
| **Speed** | Speed | 0.02 to 9.50 | 0.01 | Hz | 1.00 |
| **Depth** | Depth | 0 to 50 | 1 | — | 25 |

- **Speed** sets the rate of the LFO.
- **Depth** sets how far the LFO moves the delay. At Depth 0, the delay does not move, and the copy has no pitch change.

The Chorus has no delay time, feedback or mix parameter. The unit fixes these values.

## How it works

Claude measured these properties in the captures:

| Property | Value |
|---|---|
| Static delay | 939 device samples, 24.04 ms |
| Mix | Dry 0.75, wet 0.25. The wet copy is 9.54 dB below the dry signal. |
| Feedback | None. There is exactly one delayed copy. |
| High-frequency loss on the wet copy | None. The wet-to-dry ratio is flat to 0.013 dB from 80 Hz to 19 kHz. |
| Output channels | Left and right are the same signal |

The mix does not change with Speed or Depth.

The static delay is not a whole number of milliseconds in the delay time law of the other blocks. Thus, the model uses the fixed value of 939 device samples. One device sample more or less makes the null against the unit 15 dB worse.

The LFO moves the delay only above the static value. At the lowest point of the LFO cycle, the delay is 24.04 ms. At Depth 50, the highest point is 24.04 ms + 13.05 ms = 37.09 ms.

The Chorus uses the same LFO as the Mod Delay: the same Speed table, the same shape and the same Depth law. Claude measured each of these on the Chorus separately and compared the result with the Mod Delay:

| Property | Chorus measurement | Mod Delay table |
|---|---|---|
| Rate at Speed 1 | 0.99645 Hz | 0.9965 Hz |
| Rate at Speed 5 | 4.98977 Hz | 4.98973 Hz |
| Depth, peak to peak for each step | 10.200 device samples | 10.194 device samples |
| LFO third harmonic | −24.90 dB | −24.91 dB |
| LFO fifth harmonic | −34.45 dB | −34.43 dB |

See [4.7](README.md#47-the-lfo-and-the-speed-parameter) for the LFO laws.

## Measured accuracy

- At Depth 0, the model nulls at −31.4 dB overall against the unit. On the steady 1 kHz tone, it nulls at −59.0 dB.
- With the LFO on, the model delay follows the measured delay to within 0.05 to 0.22 device samples rms.

## Limits

- There is no measurement of the LFO rate at Speed 0.2 on the Chorus. The Mod Delay table gives this value.

See also [chapter 6](../06-limitations.md).
