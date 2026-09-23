# 5. Measurement and models

This chapter tells how the author measured the unit and made the models. It is for readers who want to know the method. You do not need it to use the plugin.

## 5.1 The principle

The firmware of the unit is in mask ROM, and the author did not read it. The author treated the unit as a black box. A black box has an input and an output, and nothing else is visible.

The method has five steps:

1. Send a fixed set of test signals into the unit.
2. Record the output of the unit. This manual calls each record a capture.
3. Measure the properties of each effect from the captures.
4. Write a model that gives the same properties, and render the test signals through it.
5. Compare the model output with the capture. The difference shows the error of the model.

## 5.2 The bench

The bench has three parts: the unit, an audio interface and a computer.

- One output of the interface goes to the guitar input of the unit.
- A second output sends the same signal back into an input of the interface. This is the reference channel.
- The left and right outputs of the unit go to two more inputs of the interface.

The interface records three channels on one clock: unit left, unit right and the reference. The analysis finds the test signal in the reference channel and uses it to align each capture. Thus, a capture can start at any time, and the exact send level is known for each capture.

The Input Level knob of the unit stays at one calibrated position, LIN, for all normal captures. LIN is 14 dB below the MAX position, and the author set it with a meter. At LIN, a full-scale 1 kHz sine from the interface is still 1.5 dB below the clip level of the unit's converter. The Output Level knob also stays at one fixed position.

## 5.3 The test signals

The normal signal set is one 48 kHz file of about 57 seconds. It has these parts, each with a gap after it for the effect tail:

| Part | Content | Use |
|---|---|---|
| Silence | 2 s | Noise floor |
| Clicks | Single-sample clicks at −16 dBFS, at irregular times | Delay times, repeat levels, impulse responses |
| Bursts | 1 kHz bursts of 60 ms at −20 dBFS, at irregular times | Pitch of each repeat |
| Sine, −20 dBFS | 1 kHz, 4 s | Level, distortion, LFO |
| Sine, −40 dBFS | 1 kHz, 2 s | Distortion at low level |
| Ramp | 1 kHz, from −40 dBFS to 0 dBFS in 5 s | Overload behavior, static gain curves |
| Sweep | Logarithmic sine sweep from 20 Hz to 19 kHz, 5 s, −20 dBFS | Frequency response |
| Noise | White noise, 3 s, −30 dBFS rms | Frequency response, detector behavior |
| Guitar | 8 s of clean direct-input guitar | Hold-out test with real music |

The clicks and bursts occur at irregular times. This makes sure that they do not all occur at the same LFO phase.

The author also used three special sets:

| Set | Content | Use |
|---|---|---|
| LFO set | A long steady tone, 30 s or 120 s | LFO rate and shape at slow speeds |
| Tail set | One click, one burst, one loud burst and one tone, each with 28 s of silence after it | High feedback values with long tails |
| Reverb set | A click, a sweep and a burst at −6 dBFS, each with 28 s of silence after it | The structure of the reverb |

## 5.4 Parameter grids

For each effect, the author wrote a grid: a list of parameter values to capture. Each row of the grid is one capture. A row changes one parameter and keeps the others at fixed values. For example, the Stereo Delay grid changes Balance with Feedback at 0, and changes Feedback with Balance at 25.

Rows at the MAX input position come at the end of a grid. They measure overload behavior.

## 5.5 What each measurement gives

| Property | Method |
|---|---|
| Delay time | The time between a click and its repeats |
| Feedback | The level ratio of successive repeats. For long tails, the slope of the energy against the repeat number. |
| High-frequency loss in a loop | The spectrum of each repeat divided by the spectrum of the repeat before it |
| LFO rate, depth and shape | The pitch of a long tone through the effect. The pitch change is the rate of change of the delay. Its integral gives the delay against time. |
| LFO in or after the feedback loop | The pitch of each repeat of a short burst. Inside the loop, each repeat can have a larger pitch change than the one before. |
| Filters | The sweep and noise responses of the unit, divided by the response of the unit with no effect |
| Static curves of nonlinear blocks | The slow ramp: the output level against the input level |
| Reverb structure | The impulse response of the reverb, with the known diffuser filters removed. This leaves the pattern of the comb filter taps. |

## 5.6 Fit by render

A measurement alone cannot always choose between two possible models. Then the author rendered each candidate model through the test signals and compared the result with the capture. The candidate with the smallest difference is the one that the model uses.

The author set the stop criterion before each fit: a fixed number of candidates or a fixed tolerance. The fit stops on that criterion and not on the appearance of the result.

## 5.7 Validation: the null test

The main test of a model is the null test:

1. Render the test signals through the model and the measured converter chain.
2. Align the model output with the capture in time. The alignment has a resolution of a fraction of a sample.
3. Scale the model output with one gain for the whole capture.
4. Subtract the model output from the capture. The result is the residual.
5. Compare the level of the residual with the level of the capture. The result, in dB, is the null depth.

A more negative null depth shows a smaller error. A null of −30 dB means that the residual is 30 dB below the signal. A null of 0 dB means that the model and the capture have no relation.

The guitar part of the signal set is a hold-out. The author never used it to fit a model. It shows how the model behaves on real music.

The bench itself limits the null depth. The bypass capture of the unit, with no effect, nulls at about −30 dB overall against the model of the converter chain. The noise and guitar parts have a lower limit because of the playback path of the interface. Thus, a block model that reaches about −30 dB overall is at the limit of the bench.

### Representative null depths

| Block or stage | Test | Null or error |
|---|---|---|
| Converter chain | Bypass capture, overall | −30.5 dB |
| Input stage at MAX | Level ramp into the clip | −25.6 dB |
| Stereo Delay | 20 ms, 300 ms, 500 ms, overall | −31.9 dB, −32.3 dB, −35.1 dB |
| Stereo Delay | 20 ms, steady tone | −60.3 dB |
| Stereo Delay with Ducking | Overall, all captured values | −30.9 dB to −31.8 dB |
| Stereo Mod Delay | 250 ms, Feedback 46, Depth 0, full tail | −41.3 dB |
| Mod Delay | LFO delay movement | 0.08 to 0.22 device samples rms |
| Chorus | Depth 0, overall / steady tone | −31.4 dB / −59.0 dB |
| Chorus | LFO delay movement | 0.05 to 0.22 device samples rms |
| 3-Band EQ | Frequency response, 39 captures | 0.043 dB rms |
| 3-Band EQ | Median of 47 captures, overall / steady tone | −28.3 dB / −40.7 dB |
| Reverb | Reverb tail, High Damp 0, median / best | −41.6 dB / −45.2 dB |
| Reverb | Reverb tail, High Damp 25 and 50, median | −20.2 dB |
| Compressor | Median of 9 captures, overall / ramp | −21.5 dB / −30.3 dB |
| Hyper Resonator (not in the plugin) | Effect path | 0.0 dB, no null (see its chapter) |

These numbers are measurements of the error. They do not mean that the model is identical to the unit.

## 5.8 From the reference model to the plugin

The author first wrote each model in a reference renderer, and made all the measurements and fits with it. Then the author wrote the plugin code in C++ from the reference model.

The author compared the C++ code with the reference renderer on the same signals. The difference was −98 dB or lower for every block, and often below −150 dB. Thus, the plugin code is a correct copy of the models, and the null depths above apply to the plugin.

## 5.9 The hardware facts

The service manual of the unit gives these facts. The author used them to check the measurements.

| Part | Fact |
|---|---|
| Sample clock | A 10 MHz system clock, divided by 256, gives 39,062.5 Hz. The author confirmed the rate with an oscilloscope. |
| Analog-to-digital converter | SAA7366T, 18-bit bitstream |
| Digital-to-analog converter | TDA1386T, 18-bit, 4-times oversampling and noise shaper |
| Delay memory | 128 KB of DRAM |
| Input stage | A buffer, the Input Level control, then an amplifier with +15 dB of gain and pre-emphasis |
| Peak LED | Driven by the overload output of the converter, at 1 dB below full scale |

## 5.10 The converter chain

The author measured the response of the unit with no effect, from its input to its output. This is the converter chain. The model has two parts:

- Three first-order high-pass filters at 5.49 Hz. They give the low-frequency response.
- A filter that gives the high-frequency response.

| Frequency | Response relative to 1 kHz |
|---|---|
| 20 Hz | −0.94 dB |
| 100 Hz | −0.06 dB |
| 10 kHz | +0.04 dB |
| 18 kHz | −0.12 dB |
| 19 kHz | −1.09 dB |
| 19.5 kHz | −10.3 dB |
| 21 kHz | −30.6 dB |

The unit has a latency of 0.255 ms from input to output. The plugin includes this latency.

### The delay clock

The measured delay times gave a law of 39 device samples for each displayed millisecond, plus 2 samples. The author found this law with three captures at 20 ms, 300 ms and 500 ms. The author then tested offsets of 1, 2 and 3 samples. The offset of 2 samples gave a null more than 20 dB better than the other two.

The Chorus gave an independent check. Its fixed delay of 939 samples is exact only at the nominal device rate.

## 5.11 The input stage

The author measured the input stage from two bypass captures: one at LIN and one at MAX.

- **The gain difference.** MAX is 14.05 dB above LIN. The difference is flat within ±0.002 dB from 20 Hz to 7 kHz.
- **The clip.** The overload is a hard clip. On the 1 kHz ramp, the measured compression follows the law of a hard clip within 0.019 dB rms over 18 dB of overload. A soft clip (tanh) gives 0.80 dB rms, which is 42 times worse.
- **The pre-emphasis.** A first-order shelf filter before the clip. Its zero is at 2489 Hz and its pole is at 8759 Hz, with a total lift of 10.9 dB. The author found it with two independent methods. One method uses the clip point of the sweep at each frequency. The other uses the level of each harmonic of the clipped ramp.
- **A check against the schematic.** The component values in the service manual give a pole time constant of 18.33 µs. The measurement gives 18.17 µs. They agree to 0.9 %.
- **The Peak LED.** The rule "1 dB below full scale, after the pre-emphasis" predicts the Peak LED threshold at five frequencies. Each prediction is inside the measured range.
- **The recovery after a loud click.** A loud click at MAX has a tail of about 20 ms. The clip removes part of the click, and this leaves a small step of DC. The three 5.49 Hz high-pass filters of the converter chain then remove this DC slowly. The model has no special recovery part, and it gives the same tail.

The clip occurs on the device-rate sample grid. Harmonics above the Nyquist frequency fold back into the audio band, as on the unit. A version of the model with the clip at a higher sample rate put these folded harmonics 8 dB to 14 dB below the capture. The version on the device-rate grid put them within 3 dB.
