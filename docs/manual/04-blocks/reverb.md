# Reverb (REV)

## What it does

The Reverb adds a simulated room sound to the signal. It has three types: ROOM, HALL and PLATE.

The Reverb has a mono input and a stereo output. The left and right outputs are different signals. On the unit, the Reverb is a member of the Ambience group.

## Parameters

| Name in the plugin | Name on the unit | Range | Step | Unit | Default |
|---|---|---|---|---|---|
| **Type** | Type | 0 to 2 | 1 | — | 1 (HALL) |
| **Pre Dly** | Pre Dly | 1 to 100 | 1 | ms | 1 |
| **Rev Time** | Rev Time | 0.1 to 10.0 | 0.1 | s | 2.0 |
| **High Damp** | High Damp | 0 to 50 | 1 | — | 0 |
| **Balance** | Balance | 0 to 50 | 1 | — | 25 |

The Type knob shows a number:

| Type value | Reverb type |
|---|---|
| 0 | ROOM |
| 1 | HALL |
| 2 | PLATE |

- **Type** selects the reverb type.
- **Pre Dly** sets the time before the reverb starts.
- **Rev Time** sets the decay time of the reverb.
- **High Damp** sets how fast the high frequencies decay, relative to the low frequencies.
- **Balance** sets the mix of dry signal and reverb signal. At 0 you hear only the dry signal. At 50 you hear only the reverb.

In the host, the Type parameter has the name "Reverb Type".

## How it works

The Reverb of the unit is a Schroeder reverberator. The author found its structure from impulse responses of the unit.

### Signal flow

1. The block adds the left and right inputs together and divides by 2.
2. The Pre Dly stage delays the mono signal. It uses the delay time law in [4.3](README.md#43-the-delay-time-law).
3. Four comb filters in parallel get the delayed signal. Each comb filter is a delay line with its own feedback loop.
4. The left output reads each comb filter at one position. The right output reads each comb filter at a different position. Each output adds its four reads with a weight of 0.5 each.
5. In HALL only, each output has an extra fixed delay.
6. Each output goes through three allpass filters in series. The left and right outputs have different allpass filters.
7. The block inverts the reverb signal and mixes it with the dry signal with the Balance tables.

The two outputs read the same four comb filters at different positions. Thus, the left and right reverb tails are different, but they decay in the same way.

### The numbers for each type

| Type | Comb filter delays | Extra delay, left / right |
|---|---|---|
| ROOM | 23.0, 27.2, 43.2, 50.0 ms | none |
| HALL | 110.0, 120.0, 128.1, 147.0 ms | 9.04 ms / 12.03 ms |
| PLATE | 50.0, 51.0, 110.0, 113.6 ms | none |

| Type | Left allpass filters (length in device samples / coefficient) | Right allpass filters |
|---|---|---|
| ROOM | 151 / 0.7544, 226 / 0.6506, 265 / 0.6523 | 130 / 0.7126, 226 / 0.6492, 265 / 0.6569 |
| HALL | 809 / 0.8007, 917 / 0.6518, 1955 / 0.6058 | 809 / 0.8024, 917 / 0.6497, 1759 / 0.5019 |
| PLATE | 923 / 0.7777, 1017 / 0.7010, 1330 / 0.6405 | 1505 / 0.7003, 1017 / 0.7000, 1330 / 0.6405 |

The left and right outputs share two of their three allpass lengths. For these shared lengths, the author measured the coefficient on each channel separately. The two values agree to 0.3 %.

The ROOM comb filters are short. The sound goes around the ROOM loops about 40 times each second, and about 8 times each second in HALL.

### Rev Time

The block sets the feedback gain of each comb filter so that the comb decays by 60 dB in the Rev Time:

    comb gain = 10^(−3 × comb length / (Rev Time × device rate))

This is one law for all three types. The author checked it at Rev Time 0.1, 0.3, 0.5, 1, 1.5, 2, 3, 5, 7 and 10 s.

These special cases apply:

- At Rev Time 0.1 s, the comb loops are effectively off. Only the decay of the allpass filters remains. This decay is about 0.9 s in HALL and about 0.6 s in PLATE.
- In PLATE, the two short comb filters (50.0 ms and 51.0 ms) decay twice as fast as the Rev Time. The law uses twice their length for them. The two long comb filters decay in the Rev Time.

### High Damp

High Damp adds a one-pole low-pass filter in the feedback loop of each comb filter. The filter has no effect at 0 Hz. At High Damp 0, there is no filter.

The pole of each filter is:

    pole = 0.017373 × High Damp × (comb length / longest comb length of the type)

Thus, the longest comb of each type gets the largest pole: 0.434 at High Damp 25 and 0.869 at High Damp 50. Each shorter comb gets a smaller pole in proportion to its length. The same law applies to all three types.

Because the filter is in the loop, the high frequencies lose more level with each pass. The table shows the measured decay rate of the HALL reverb at Rev Time 10, in dB per second:

| High Damp | 250 Hz | 500 Hz | 2 kHz |
|---|---|---|---|
| 0 | −5.7 | −5.9 | −6.2 |
| 25 | −5.6 | −5.9 | −8.9 |
| 50 | −5.9 | −7.5 | −22.6 |

### Balance

The Reverb has its own Balance tables. They are not the same as the delay tables.

| Balance | Wet gain | Dry gain |
|---|---|---|
| 0 | 0.0000 | 1.0000 |
| 5 | 0.0919 | 0.9626 |
| 10 | 0.1834 | 0.9252 |
| 15 | 0.2749 | 0.8877 |
| 20 | 0.4753 | 0.8278 |
| 25 | 0.7031 | 0.7629 |
| 35 | 0.8435 | 0.4680 |
| 45 | 0.9478 | 0.1558 |
| 50 | 1.0000 | 0.0000 |

The block interpolates in a straight line between the points.

## Measured accuracy

- At High Damp 0, the reverb tail of the model nulls against the unit at −41.6 dB median and −45.2 dB at best. The captures cover all three types, Rev Time 0.1 to 10, and all the Pre Dly and Balance values that the author tested.
- At High Damp 25 and 50, the tail nulls at −20.2 dB median and −38.5 dB at best, over 11 captures.

## Limits

- A long, loud signal makes the reverb path of the unit compress. The author measured up to 9 dB of compression in ROOM and PLATE with a −6 dBFS sweep. The model does not compress.
- The author captured High Damp only at 0, 25 and 50. The model is linear between these values.
- ROOM gives the weakest null of the three types, mostly at short Rev Time values.
- The author did not capture Balance 30 and 40. The model interpolates these values.

See also [chapter 6](../06-limitations.md).
