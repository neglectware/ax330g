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
# another project's CMakeLists.txt (2026-09-16
# build 12) -- found here 2026-09-16 while bumping to 0.5.1/9: the installed
# bundle still read 0.4.2/7 after a `make-installer.sh` run that DID pick up
# a source change and DID name the .pkg 0.5.1.9 correctly (that name comes
# from make-installer.sh's own `sed` read of version.h, a separate path that
# was never affected).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h")

# Deployment floor macOS 10.13 (a known user runs 10.13.6); universal so one
# .pkg covers Intel and Apple silicon Macs. Cache vars so either can still be overridden on the command line
# (-DCMAKE_OSX_DEPLOYMENT_TARGET=... / -DCMAKE_OSX_ARCHITECTURES=...). Must
# be set before project() to take effect.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment target")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS build architectures")

project(AX330G VERSION ${AX_VERSION})
set(CMAKE_CXX_STANDARD 17)
if(NOT DEFINED JUCE_DIR)
  set(JUCE_DIR "$ENV{HOME}/Developer/JUCE")
endif()
add_subdirectory(${JUCE_DIR} JUCE)

juce_add_plugin(AX330G
  COMPANY_NAME "Neglectware"   # the vendor hosts group plugins under (2026-09-23). Hosts identify the plugin by the codes below, not the bundle ID -- keep Mobr/Ax33 or saved sessions lose it
  PLUGIN_MANUFACTURER_CODE Mobr
  PLUGIN_CODE Ax33
  FORMATS AU VST3 Standalone
  PRODUCT_NAME "AX330G"
  BUNDLE_ID com.neglectware.ax330g
  VERSION ${AX_VERSION}
  BUILD_VERSION "${AX_VERSION}.${AX_BUILD}"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  COPY_PLUGIN_AFTER_BUILD TRUE
  AU_MAIN_TYPE kAudioUnitType_Effect
  VST3_CATEGORIES Fx Delay)

target_sources(AX330G PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp src/lcd/LcdDisplay.cpp src/lcd/LcdNativeLayer_mac.mm)
target_include_directories(AX330G PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(AX330G PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(AX330G PRIVATE
  juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

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
