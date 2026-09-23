# Stereo Chorus (SCHO)

NOTE: The unit does not have a Stereo Chorus. This block is a "what-if" block. It shows how a stereo version of the unit's Chorus can sound. It is not an emulation of a real effect, and nobody can measure it against hardware.

## What it does

The Stereo Chorus is the [Chorus](chorus.md) of the unit with a stereo output. It has a mono input. The **Mode** parameter selects one of two ways to make the stereo output.

The author made this block from measured parts of the unit:

- The delay, the mix, the LFO and the Depth law come from the Chorus.
- The inverted LFO for the right channel comes from the Stereo Mod Delay.

## Parameters

| Name in the plugin | Range | Step | Unit | Default |
|---|---|---|---|---|
| **Speed** | 0.02 to 9.50 | 0.01 | Hz | 1.00 |
| **Depth** | 0 to 50 | 1 | — | 25 |
| **Mode** | 0 to 1 | 1 | — | 0 |

- **Speed** sets the rate of the LFO.
- **Depth** sets how far the LFO moves the delay.
- **Mode** selects the stereo method. The knob shows the number of the mode.

| Mode | Name | Left output | Right output |
|---|---|---|---|
| 0 | Inverted LFO | Dry 0.75 + a wet copy that the LFO moves | Dry 0.75 + a second wet copy that the inverted LFO moves |
| 1 | Split | Dry only, at 0.75 | Wet only, at 0.25 |

### Mode 0: Inverted LFO

The block reads the delay line at two positions. The left position follows the LFO. The right position follows the LFO with its sign changed. When the left copy goes up in pitch, the right copy goes down.

Each channel has the Chorus mix of 0.75 dry and 0.25 wet. The two channels differ only in their wet copies.

### Mode 1: Split

The left channel has only the dry signal. The right channel has only the wet copy of the Chorus. This is the method of some classic stereo chorus pedals.

In this mode, the sum of the left and right channels is exactly the output of the mono Chorus at the same Speed and Depth. Thus, a mono mix of the output sounds the same as the mono Chorus.

## How it works

The block has one delay line with a fixed delay of 939 device samples (24.04 ms), the same as the Chorus. There is no feedback and no high-frequency loss. The LFO uses the Speed table, shape and Depth law in [4.7](README.md#47-the-lfo-and-the-speed-parameter).

## Limits

- The unit does not have this block. A future "As the unit" mode will not offer it.
