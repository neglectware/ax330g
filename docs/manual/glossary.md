# Glossary

This glossary tells what each technical name in this manual means.

| Term | Definition |
|---|---|
| 14-segment display | An LED display with 14 segments and a decimal point. It can show each letter in a different shape. The **BANK** display is of this type. |
| 18-bit | A word length of 18 binary digits for each sample. The converters of the unit use this word length. |
| 7-segment display | An LED display with seven segments. It shows digits. The **SLOT** display is of this type. |
| Allpass filter | A filter that does not change the level of any frequency. It changes the phase. |
| Ambience | The group of the unit that contains the delays and the reverb. |
| Amplitude | The size of a signal, as a linear value, not in dB. |
| Apple silicon | The processors that Apple designs for Mac computers. |
| Attack | The time that a detector or a gain control takes to react to a signal that increases. |
| AU | Audio Unit. A plugin format of Apple for macOS. |
| Backlight | The light behind an LCD. |
| Balance | The parameter that sets the mix of the dry signal and the wet signal. |
| Band-pass filter | A filter that lets a band of frequencies through and decreases the frequencies above and below the band. |
| Bank | A folder of presets with a letter from A to Z. Each preset in a bank has a number from 001 to 999. The unit also keeps its programs in banks. |
| Behavioral emulation | A copy of what a device does, made from measurements of its output. It does not use the internal program of the device. |
| Bitstream converter | A type of analog-to-digital converter that operates at a high rate with few bits, then filters the result. |
| Black box | A device that you examine only through its input and output. |
| Block | One effect in the plugin, for example the Stereo Delay. Also, on the unit, a part of the chain (Block 1, Block 2). |
| Block 1 | The first part of the unit's chain. It contains the compressor, distortions, wah, 3-band EQ or Hyper Resonator. |
| Burst | A short tone, for example 60 ms of a 1 kHz sine. |
| Capture | A record of the output of the unit for one parameter value and one set of test signals. |
| Cent | One hundredth of a semitone. 1200 cents are one octave. |
| Chain | The order of effects that the signal goes through. |
| Chain line | Line 2 of the LCD play page. It shows the blocks in slot order. |
| Click | A test signal of one sample with a high value and all other samples at zero. |
| Clip | A limit on the signal level. The signal cannot go above the limit. |
| Comb filter | A delay line with feedback. Its frequency response has many equal peaks, like the teeth of a comb. |
| Compressor | An effect that makes the level of a signal more constant. |
| Converter | An analog-to-digital converter (ADC) or a digital-to-analog converter (DAC). |
| Converter chain | The model of the frequency response of the converters and analog circuits of the unit, from input to output, with no effect. |
| Corner frequency | The frequency at which a filter starts to change the level. |
| Crossfade | A mix in which one signal decreases while the other increases. |
| dB | Decibel. A logarithmic unit for a ratio of levels. |
| dBFS | Decibels relative to full scale. 0 dBFS is the largest level that a digital system can show. |
| dBu | Decibels relative to 0.775 V rms. A unit for analog signal levels. |
| De-emphasis | A filter that decreases the treble by the same quantity that a pre-emphasis filter increased it. |
| Decay | The decrease of a signal level with time. |
| Default | The value that a parameter gets when you select a block. |
| Delay line | A memory that gives a signal back after a set time. |
| Depth | The parameter that sets how far an LFO moves a delay or a filter. |
| Detector | A part of an effect that measures the level of a signal. |
| Device rate | The internal sample rate of the unit, 39,062.5 Hz. The plugin runs its blocks at this rate. |
| Device sample | One sample at the device rate. It is 0.0256 ms long. |
| DI (direct input) | A guitar signal recorded directly from the guitar, with no amplifier or effect. |
| DRAM | Dynamic random-access memory. The unit keeps its delayed audio in DRAM. |
| Dry signal | The signal without the effect. |
| DSP | Digital signal processor. The chip that calculates the effects in the unit. |
| Ducking | A function that decreases the level of the effect signal when the input level increases. |
| Envelope | A smooth curve that follows the level of a signal. |
| Equal-power crossfade | A crossfade that keeps the total power constant. |
| Even harmonics | The harmonics at 2, 4, 6 and more times the fundamental frequency. |
| Feedback | The part of the output of a delay that goes back to its input. It makes repeats. |
| Firmware | The program inside a device. |
| First-order | The simplest form of a filter. Its slope is 6 dB for each octave. |
| Fold back | The effect of a frequency above the Nyquist frequency that appears at a lower frequency after a clip at a given sample rate. |
| Full scale | The largest value that a converter or a digital signal can have. |
| Full-wave rectifier | A circuit or function that makes all values of a signal positive. On a sine, it doubles the frequency. |
| Fundamental | The lowest frequency of a tone. It sets the pitch. |
| Gain | A change of level, as a multiplier or in dB. |
| Hard clip | A clip with a sharp limit. The signal follows the input exactly up to the limit and stays at the limit above it. |
| Harmonics | Frequencies at whole multiples of the fundamental. A distortion adds them. |
| HD44780 | A standard controller chip for character LCDs. The unit uses a display of this type. |
| Headroom | The level difference between a signal and the clip level. |
| High Damp | The parameter that sets the loss of high frequencies in a delay or reverb loop. |
| High-pass filter | A filter that decreases the frequencies below its corner frequency. |
| Hold-out | A test signal that Claude did not use to fit a model. It shows how the model behaves on new material. |
| Host | The application that loads the plugin, for example Logic Pro or MainStage. |
| Impulse response | The output of a system for a click at its input. It shows the full linear behavior of the system. |
| Input stage | The model of the analog input circuit and the analog-to-digital converter of the unit. |
| Interpolation | The calculation of a value between two known values. Linear interpolation uses a straight line. |
| IPE | A library of preset effect variations in the unit. |
| JSON | A text format for data. A preset file uses it. |
| Knee | The region where a compressor or a clip starts to act. |
| Latency | The delay between the input and the output of the plugin. |
| LCD | Liquid crystal display. |
| LFO | Low-frequency oscillator. A slow oscillator that moves a parameter, for example a delay time. |
| Limiter | An effect that keeps the output level below a fixed level. |
| LIN | The reference position of the Input Level knob of the unit, 14 dB below MAX. Input 0 dB in the plugin. |
| Linear | The output is in proportion to the input. |
| Loop gain | The gain of a feedback loop for one pass. |
| Low-pass filter | A filter that decreases the frequencies above its corner frequency. |
| Mask ROM | A memory that the manufacturer writes when it makes the chip. Nobody can change it after that. |
| MAX | The fully clockwise position of the Input Level knob of the unit. Input +14 dB in the plugin. |
| Minimum phase | A filter that has the smallest possible phase change for its level response. |
| Mod1 | The group of the unit that contains the chorus, flanger, phaser, vibrato, tremolo and ring modulator. |
| Mod2 | The group of the unit that contains the modulation delays and other stereo modulation effects. |
| Mono | One audio channel. |
| Nonlinear | Not in proportion to the input. A clip is nonlinear. |
| Null depth | The level of the residual relative to the level of the capture, in dB. |
| Null test | A test that subtracts a model output from a capture of the real device. |
| Nyquist frequency | Half of the sample rate. At the device rate, it is 19,531.25 Hz. |
| Octave | A ratio of 2 between two frequencies. |
| Odd harmonics | The harmonics at 3, 5, 7 and more times the fundamental frequency. |
| One-pole filter | A first-order filter with one pole. |
| Open mode | The mode in which each slot can hold any block, in any order. The **OPEN MODE** key sets it on or off. |
| Oversampling | An operation at a sample rate higher than necessary, to make filters easier. |
| Peak | The highest absolute value of a signal. |
| Peak detector | A detector that follows the peaks of a signal, not its average. |
| Peak filter | A filter that increases or decreases a band of frequencies around a center frequency. |
| Peak LED | The indicator that comes on when the input is near the clip level. |
| Phase | The time position of a periodic signal in its cycle. |
| Plugin | A program that adds a function to a host. |
| Plugin instance | One copy of the plugin in one slot of a host. |
| Pole | A value that sets the response of a filter. In a one-pole low-pass filter, a pole near 1 gives a low corner frequency. |
| Pre-delay | The time between the input signal and the start of the reverb. |
| Pre-emphasis | A filter that increases the treble before a converter. The unit uses it to decrease the noise at high frequencies. |
| Preset | A file that holds the blocks, the parameter values and the mode of the eight slots. The plugin equivalent of a program on the unit. |
| Program | A stored set of chain and parameter values on the unit. |
| Q | A number that shows how narrow the peak of a filter is. A high Q gives a narrow, high peak. |
| Quantizer | A function that rounds a signal to a fixed word length. |
| Ramp | A test tone whose level increases smoothly with time. |
| Reference channel | A channel of the capture that records the test signal directly, not through the unit. |
| Release | The time that a detector or a gain control takes to react to a signal that decreases. |
| Repeat | One echo from a delay. |
| Residual | The signal that remains after the null test subtracts the model output from the capture. |
| Resonator | A filter with a high peak at one frequency. |
| Rest frequency | The corner frequency of the Hyper Resonator when the input is quiet. |
| Rev Time | The time that the reverb takes to decrease by 60 dB. |
| rms | Root mean square. A measure of the average level of a signal. |
| RT60 | The time for a sound to decrease by 60 dB. |
| Sample rate | The number of samples each second in a digital signal. |
| Sample rate converter | A function that changes a signal from one sample rate to another. |
| Schroeder reverberator | A reverb design with parallel comb filters followed by allpass filters in series. |
| Sensitivity | A parameter that sets how strongly an effect reacts to the input level. |
| Shelf filter | A filter that increases or decreases all frequencies above or below a corner frequency by a fixed quantity. |
| Signal set | The fixed file of test signals for all captures. Claude designed it. |
| Sine | A pure tone with one frequency. |
| Slot | One of the eight positions in the plugin that can hold a block. |
| Smoother | A filter that makes a control signal change more slowly. |
| Soft clip | A clip that curves gradually into its limit. |
| Speed | The parameter that sets the rate of an LFO. |
| Static curve | The output level of a nonlinear block against its input level, for slow signals. |
| Stereo | Two audio channels, left and right. |
| Sustain | The time that a note continues at an audible level. |
| Sweep | A test tone whose frequency changes smoothly with time. Also, the movement of a filter frequency. |
| Tail | The output that continues after the input stops, for example the echoes of a delay. |
| Threshold | A level above which an effect starts to act. |
| Time constant | The time that an exponential change takes to reach 63 % of its final value. |
| Time-variant | The behavior changes with time. |
| Truncation | A reduction of word length that removes the lowest bits without a round operation. |
| Unit | In this manual, the original Korg Toneworks AX30G or AX300G. |
| Universal binary | A plugin file that contains programs for more than one processor type. |
| VST3 | A plugin format that many hosts use. |
| Wet signal | The signal that the effect makes. |
| White noise | A random signal with equal power at all frequencies. |
| Word length | The number of bits in each sample. |
| Zero | A value that sets the response of a filter. It is the opposite of a pole. |
