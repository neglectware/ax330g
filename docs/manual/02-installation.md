# 2. Installation

## 2.1 System requirements

| Item | Requirement |
|---|---|
| Computer | A Mac with an Apple silicon processor or an Intel processor |
| Operating system, Apple silicon | macOS 11 or later |
| Operating system, Intel | macOS 10.13 or later (see the note below) |
| Operating system, Windows | 64-bit Windows, VST3 hosts only (see 2.6) |
| Host | An application that loads AU plugins or VST3 plugins |
| Audio channels | Stereo output. Stereo input or mono input. |

The plugin is a universal binary. It contains a program for Apple silicon and a program for Intel processors. The installer puts both programs on the computer, and macOS selects the correct one.

NOTE: The Intel program has a minimum system version of macOS 10.13. Nobody has tested the plugin on macOS 10.13. Mark O'Brien uses the plugin on a later version of macOS on Apple silicon.

## 2.2 The installer

The installer is one file with the name `AX330G-<version>.<build>.pkg`. For this version, the name is `AX330G-0.8.6.19.pkg`.

The installer does not have an Apple Developer ID signature. For this reason, macOS does not open it with a normal double-click. Use the procedure that follows.

1. Quit all hosts.
2. Find the installer file in the Finder.
3. Hold the Control key and click the installer file.
4. Select **Open** from the menu.
5. Click **Open** in the dialog that macOS shows.
6. Follow the steps in the installer.
7. Type your administrator password when the installer asks for it.
8. Start the host.

The installer puts two files on the computer:

| Format | Location |
|---|---|
| AU | `/Library/Audio/Plug-Ins/Components/AX330G.component` |
| VST3 | `/Library/Audio/Plug-Ins/VST3/AX330G.vst3` |

After it copies the files, the installer stops the macOS process that keeps the list of AU plugins. This makes sure that the host finds the new version.

## 2.3 Find the plugin in the host

The plugin is an audio effect. It is not an instrument, and it does not use MIDI.

The host shows the plugin under the manufacturer name "Neglectware". The plugin name is "AX330G". In the host's plugin menu, find **Neglectware**, then **AX330G**.

To add the plugin in Logic Pro or MainStage:

1. Click an empty Audio FX slot on a channel strip.
2. Select **Audio Units**.
3. Select **Neglectware**.
4. Select **AX330G**.
5. Select **Stereo** if the host asks for a channel format.

In a VST3 host, find "AX330G" in the list of VST3 effects.

NOTE: If the host does not show the plugin, quit the host and start it again. The host then examines its plugin folders again.

## 2.4 Update the plugin

To install a newer version, do the installation procedure again with the new installer. The new files replace the old files.

A new version keeps the same plugin identity. Thus, the host opens projects that you saved with an older version, and the parameter values stay the same.

NOTE: Versions before 0.8.6 used the manufacturer name "Mark O'Brien". From version 0.8.6, the host shows "Neglectware". The plugin identity did not change, so saved projects continue to find the plugin.

## 2.5 Remove the plugin

1. Quit all hosts.
2. Move `/Library/Audio/Plug-Ins/Components/AX330G.component` to the Trash.
3. Move `/Library/Audio/Plug-Ins/VST3/AX330G.vst3` to the Trash.
4. Type your administrator password when the Finder asks for it.

## 2.6 Windows

A Windows version is available in the VST3 format, for 64-bit Windows. GitHub Actions compiles it on a Windows build server.

WARNING: Nobody has tested the Windows version on a Windows computer yet. Please report the results to the project.

To install the Windows version:

1. Download the Windows zip file from the Releases page of the project.
2. Unzip the file.
3. Copy the `AX330G.vst3` folder to `C:\Program Files\Common Files\VST3\`.
4. Start the host again, or tell it to scan for plugins.
5. Find the plugin under Neglectware > AX330G.

NOTE: The Windows version is not signed. Windows or the host can show a warning when you load it for the first time.

NOTE: The Windows version has no AU format, because AU is only for macOS. The LCD uses a simpler drawing method on Windows. It shows the same content.
