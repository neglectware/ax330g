# AX30G Bench - AppKit front end for the capture workflow. `make app` builds
# "AX30G Bench.app" next to this Makefile; the Python in .. is the engine.
CC      ?= clang
CFLAGS  ?= -O2 -Wall -Wno-unused-parameter
SRC     = src/main.m src/AXAudio.m src/AXAppDelegate.m
HDR     = src/AXAudio.h src/AXAppDelegate.h src/version.h
APP     = AX30G Bench.app
AX_BUILD := $(shell sed -n 's/^\#define AX_BUILD[[:space:]]*//p' src/version.h)

AX30GBench: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -fobjc-arc -Isrc -o $@ $(SRC) -framework Cocoa -framework AVFoundation -framework CoreAudio -framework AudioToolbox -framework UniformTypeIdentifiers

app: AX30GBench
	rm -rf "$(APP)"
	mkdir -p "$(APP)/Contents/MacOS" "$(APP)/Contents/Resources"
	cp AX30GBench "$(APP)/Contents/MacOS/AX30GBench"
	printf '%s\n' \
	  '<?xml version="1.0" encoding="UTF-8"?>' \
	  '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
	  '<plist version="1.0"><dict>' \
	  '  <key>CFBundleName</key><string>AX30G Bench</string>' \
	  '  <key>CFBundleIdentifier</key><string>com.neglectware.ax30g-bench</string>' \
	  '  <key>CFBundleExecutable</key><string>AX30GBench</string>' \
	  '  <key>CFBundlePackageType</key><string>APPL</string>' \
	  "  <key>CFBundleShortVersionString</key><string>build $(AX_BUILD)</string>" \
	  '  <key>NSMicrophoneUsageDescription</key><string>Records the AX30G outputs and the reference channel.</string>' \
	  '  <key>NSHighResolutionCapable</key><true/>' \
	  '  <key>LSMinimumSystemVersion</key><string>13.0</string>' \
	  '</dict></plist>' > "$(APP)/Contents/Info.plist"
	codesign --force --sign - "$(APP)" 2>/dev/null || true

run: app
	open "$(APP)"

clean:
	rm -rf AX30GBench "$(APP)"
