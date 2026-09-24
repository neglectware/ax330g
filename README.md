# AX330G

AX330G is an audio plug-in. It emulates the effects of the Korg Toneworks AX30G and AX300G guitar multi-effects units (1997). It is available for macOS (AU and VST3) and for Windows (VST3, 64-bit).

NOTE: One tester installed version 0.8.6 on a Windows computer. The plugin loaded and its sound was correct. Nobody has tested version 0.9.0 (the new editor) on Windows yet.

Each effect was measured on a real AX300G and then modeled. The unit was a "black box": no firmware was read or copied. The models were tested against recordings of the real unit.

AX330G is an unofficial project. It is not affiliated with or endorsed by Korg. Korg, Toneworks, AX30G and AX300G are trademarks of their owners.

AX330G is an AI-authored, human-directed project, published by Neglectware. Mark O'Brien owns the AX300G, did every capture by hand at the unit, judged the results and directed the project. Claude, an AI model from Anthropic, designed the tests, wrote all the software, did the analysis and the model fits, and wrote the manual.

![The AX330G editor](docs/manual/images/editor.png)

## Status

AX330G is in development. This table shows the effects ("blocks") that are in the plug-in now.

| Block | On the unit |
|---|---|
| Stereo Delay (with Ducking) | Yes |
| Mod Delay | Yes |
| Stereo Mod Delay | Yes |
| Chorus | Yes |
| Stereo Chorus | No. This block is a "what if" addition. |
| 3-Band EQ | Yes |
| Reverb | Yes |
| Compressor | Yes |

The Hyper Resonator is measured but it is not in the plug-in yet. The other effects of the unit are not measured yet.

## Documentation

Read the [manual](docs/manual/README.md). It tells you how to install and use the plug-in. It also lists all the parameters of each block and tells you how each block works. The chapter [How it was modeled](docs/manual/05-how-it-was-modelled.md) tells you how the measurements and models were made.

The manual uses ASD-STE100 Simplified Technical English.

## Install

Download the latest version from [Releases](https://github.com/neglectware/ax330g/releases). For macOS, open the `.pkg` file. For Windows, unzip the file and read `INSTALL-Windows.txt`.

## Build from source

You need:

- macOS with Xcode and the Xcode command-line tools.
- CMake 3.22 or later.
- JUCE 8.0.8. Put it in `~/Developer/JUCE`, or give its location with `-DJUCE_DIR=<path>`.

Do these steps:

1. Open Terminal in the `plugin-chain` folder.
2. Type `cmake -B build-release -G Xcode`.
3. Type `cmake --build build-release --config Release --target AX330G_AU AX330G_VST3`.

The build copies the AU and VST3 plug-ins into `~/Library/Audio/Plug-Ins`. GitHub Actions builds the Windows version: read `.github/workflows/build.yml`. To make an installer package, type `tools/make-installer.sh` in the project folder.

## What is in this repository

| Folder | Contents |
|---|---|
| `plugin-chain/` | The plug-in (JUCE): the processor, the editor and the LCD emulation. |
| `dsp/` | The effect models in C++. The plug-in uses these files. |
| `engine/` | The reference models in Python. |
| `models/` | The model parameters (JSON). |
| `capture/` | The test signals and the measurement grids. |
| `analysis/`, `tests/` | The analysis scripts and the null tests. |
| `app/` | AX30G Bench, the macOS app that records the measurements. |
| `tools/` | Command-line renderers, the LCD data tools and the installer script. |
| `docs/manual/` | The manual. |

The recordings of the real unit are not in this repository. They are too large.

## License

AX330G is free software under the GNU Affero General Public License, version 3 (AGPL-3.0). Read the [LICENSE](LICENSE) file. The plug-in uses the JUCE framework, which is available under the AGPL-3.0.

Copyright © 2026 Mark O'Brien. Published by Neglectware.
