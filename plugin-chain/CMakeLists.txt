cmake_minimum_required(VERSION 3.22)

# AX_VERSION / AX_BUILD single source of truth: src/version.h. Read before
# project() so CMAKE_PROJECT_VERSION and the bundle's CFBundleVersion agree
# with what the plugin itself reports. See src/version.h for the convention.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h" AX_VERSION_LINE REGEX "#define AX_VERSION ")
string(REGEX REPLACE ".*#define AX_VERSION \"([^\"]+)\".*" "\\1" AX_VERSION "${AX_VERSION_LINE}")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h" AX_BUILD_LINE REGEX "#define AX_BUILD ")
string(REGEX REPLACE ".*#define AX_BUILD ([0-9]+).*" "\\1" AX_BUILD "${AX_BUILD_LINE}")
if(NOT AX_VERSION OR NOT AX_BUILD)
  message(FATAL_ERROR "Could not parse AX_VERSION/AX_BUILD out of src/version.h")
endif()
# file(STRINGS...) above does not itself trigger a reconfigure when
# version.h changes -- CMake only reconfigures when a tracked file changes.
# Without this, a persistent build directory (tools/make-installer.sh only
# runs `cmake -B` when build-release doesn't already exist) silently keeps
# stamping the FIRST version.h it ever saw into CFBundleShortVersionString/
# CFBundleVersion, even though the actual object code rebuilds correctly on
# every source change. Same bug, same fix, as
# another project's CMakeLists.txt (2026-09-16) -- found here 2026-09-16 while bumping to 0.5.1/9: the installed
# bundle still read 0.4.2/7 after a `make-installer.sh` run that DID pick up
# a source change and DID name the .pkg 0.5.1.9 correctly (that name comes
# from make-installer.sh's own `sed` read of version.h, a separate path that
# was never affected).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h")

# Deployment floor macOS 10.13 (a known user runs 10.13.6); universal so one
# .pkg covers Intel and Apple silicon Macs. Cache vars so either can still be overridden on the command line
# (-DCMAKE_OSX_DEPLOYMENT_TARGET=... / -DCMAKE_OSX_ARCHITECTURES=...). Must
# be set before project() to take effect. Apple-only: these variables mean
# nothing to the other generators (Windows CI uses the Visual Studio
# generator, x64 chosen with cmake -A).
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment target")
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS build architectures")
endif()

project(AX330G VERSION ${AX_VERSION})
set(CMAKE_CXX_STANDARD 17)
if(NOT DEFINED JUCE_DIR)
  set(JUCE_DIR "$ENV{HOME}/Developer/JUCE")
endif()
add_subdirectory(${JUCE_DIR} JUCE)

# AU is a macOS-only plug-in format. Elsewhere the plug-in is VST3 (+
# Standalone, which costs nothing extra as a CMake target even though only
# VST3 is actually built in CI on Windows, .github/workflows/build.yml).
if(APPLE)
  set(AX_FORMATS AU VST3 Standalone)
  set(AX_COPY_PLUGIN_AFTER_BUILD_DEFAULT ON)
  # macOS only: a Core Animation layer (LcdNativeLayer_mac.mm) keeps the LCD
  # start-up animation on time when the host's main thread is busy (see
  # src/lcd/LcdNativeLayer.h). Elsewhere LcdNativeLayer_stub.cpp's create()
  # returns nullptr and LcdDisplay falls back to its juce::Timer + repaint()
  # path -- already how it behaves on macOS too, when create() fails there.
  set(AX_LCD_NATIVE_SRC src/lcd/LcdNativeLayer_mac.mm)
else()
  set(AX_FORMATS VST3 Standalone)
  # COPY_PLUGIN_AFTER_BUILD installs into system plug-in folders
  # (e.g. C:\Program Files\Common Files\VST3 on Windows) -- fine on a
  # developer's own Mac, not something a CI runner should be doing.
  set(AX_COPY_PLUGIN_AFTER_BUILD_DEFAULT OFF)
  set(AX_LCD_NATIVE_SRC src/lcd/LcdNativeLayer_stub.cpp)
endif()
# Overridable (-DAX_COPY_PLUGIN_AFTER_BUILD=OFF) so a CI compile-check job on
# macOS -- .github/workflows/build.yml's macos-latest job -- can skip
# installing into ~/Library on the runner without changing Mark's own local
# build, which takes the default above (ON, exactly as before this option
# existed) and behaves identically.
option(AX_COPY_PLUGIN_AFTER_BUILD "Install the built plug-in into the system's plug-in folders" ${AX_COPY_PLUGIN_AFTER_BUILD_DEFAULT})

juce_add_plugin(AX330G
  COMPANY_NAME "Neglectware"   # the vendor hosts group plugins under (2026-09-23). Hosts identify the plugin by the codes below, not the bundle ID -- keep Mobr/Ax33 or saved sessions lose it
  PLUGIN_MANUFACTURER_CODE Mobr
  PLUGIN_CODE Ax33
  FORMATS ${AX_FORMATS}
  PRODUCT_NAME "AX330G"
  BUNDLE_ID com.neglectware.ax330g
  VERSION ${AX_VERSION}
  BUILD_VERSION "${AX_VERSION}.${AX_BUILD}"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  COPY_PLUGIN_AFTER_BUILD ${AX_COPY_PLUGIN_AFTER_BUILD}   # option() above, defaults per-platform
  AU_MAIN_TYPE kAudioUnitType_Effect
  VST3_CATEGORIES Fx Delay)

target_sources(AX330G PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp src/ui/AxUi.cpp src/lcd/LcdDisplay.cpp ${AX_LCD_NATIVE_SRC})

# The editor's fonts (2026-09-23, 0.9.0): Archivo and Archivo Narrow static
# TTFs, SIL Open Font License 1.1 (fonts/OFL.txt), embedded so the UI looks
# the same on every machine. src/ui/AxUi.cpp looks them up by file name.
juce_add_binary_data(AX330GFonts SOURCES
  fonts/Archivo-Regular.ttf fonts/Archivo-SemiBold.ttf fonts/Archivo-Bold.ttf fonts/Archivo-BlackItalic.ttf
  fonts/ArchivoNarrow-SemiBold.ttf fonts/ArchivoNarrow-Bold.ttf)
target_include_directories(AX330G PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(AX330G PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(AX330G PRIVATE
  AX330GFonts juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# The two developer-tool targets below (LCD snapshot verifier, in-process AU
# host harness) are macOS-only: the harness is Objective-C++ and links
# CoreAudioKit, and neither is shipped or needed to prove the plug-in itself
# builds on another platform -- the plugin-only Windows CI job builds just
# the AX330G_VST3 target.
if(APPLE)

# AX330GLcdSnapshot: a headless verification tool for the LCD start-up
# animation (docs/lcd-startup-2026-09-22.md, src/lcd/LcdDisplay.h) -- a
# separate console app, not part of the AX330G plugin target above, sharing
# only LcdDisplay.cpp so a build of one never depends on the other.
juce_add_console_app(AX330GLcdSnapshot
  PRODUCT_NAME "AX330GLcdSnapshot")
target_sources(AX330GLcdSnapshot PRIVATE tools/LcdSnapshot.cpp src/lcd/LcdDisplay.cpp src/lcd/LcdNativeLayer_mac.mm)
target_include_directories(AX330GLcdSnapshot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(AX330GLcdSnapshot PRIVATE
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(AX330GLcdSnapshot PRIVATE
  juce::juce_gui_basics
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# AX330GHostHarness: a minimal in-process AU host that opens the installed
# plugin's editor (tools/HostHarness.cpp) -- for seeing the editor behave as
# it does in a real host, e.g. the LCD's repaint cadence. Not shipped.
juce_add_gui_app(AX330GHostHarness
  PRODUCT_NAME "AX330GHostHarness")
target_sources(AX330GHostHarness PRIVATE tools/HostHarness.cpp)
set_source_files_properties(tools/HostHarness.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
target_compile_definitions(AX330GHostHarness PRIVATE
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1)
target_link_libraries(AX330GHostHarness PRIVATE
  juce::juce_audio_processors juce::juce_gui_basics "-framework CoreAudioKit"
  PUBLIC juce::juce_recommended_config_flags)

# AX330GMoveSlotTest: headless test of drag-to-reorder (0.9.1 build 21,
# tools/MoveSlotTest.cpp) -- builds the processor and editor from src/
# directly, drives moveSlot() and synthetic tile drags, runs audio through
# the moves and writes editor snapshots. Not shipped.
juce_add_console_app(AX330GMoveSlotTest
  PRODUCT_NAME "AX330GMoveSlotTest")
target_sources(AX330GMoveSlotTest PRIVATE tools/MoveSlotTest.cpp
  src/PluginProcessor.cpp src/PluginEditor.cpp src/ui/AxUi.cpp src/lcd/LcdDisplay.cpp src/lcd/LcdNativeLayer_mac.mm)
target_include_directories(AX330GMoveSlotTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(AX330GMoveSlotTest PRIVATE
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(AX330GMoveSlotTest PRIVATE
  AX330GFonts juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags)

endif()
