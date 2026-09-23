#!/usr/bin/env bash
set -euo pipefail

# tools/make-installer.sh — builds AX330G Release (AU + VST3, universal
# arm64+x86_64, 10.13 floor per plugin-chain/CMakeLists.txt) and packages a
# .pkg installer for anyone without a JUCE/Xcode dev setup that
# drops the plugin into /Library/Audio/Plug-Ins/{Components,VST3}.
#
# Version comes from plugin-chain/src/version.h (AX_VERSION/AX_BUILD), the
# same single source of truth CMakeLists.txt reads for the bundles'
# CFBundleShortVersionString/CFBundleVersion, so the .pkg version always
# matches what actually got built. Nothing here edits version.h — bump
# AX_BUILD by hand before a real release build.

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
PLUGIN_CHAIN="$ROOT/plugin-chain"
VERSION_H="$PLUGIN_CHAIN/src/version.h"
BUILD_DIR="$PLUGIN_CHAIN/build-release"
DIST_DIR="$ROOT/dist"
PKG_IDENTIFIER="com.neglectware.ax330g.pkg"

if [[ ! -f "$VERSION_H" ]]; then
  echo "make-installer.sh: $VERSION_H not found" >&2
  exit 1
fi

AX_VERSION="$(sed -nE 's/.*#define AX_VERSION "([^"]+)".*/\1/p' "$VERSION_H")"
AX_BUILD="$(sed -nE 's/.*#define AX_BUILD ([0-9]+).*/\1/p' "$VERSION_H")"
if [[ -z "$AX_VERSION" || -z "$AX_BUILD" ]]; then
  echo "make-installer.sh: could not parse AX_VERSION/AX_BUILD out of $VERSION_H" >&2
  exit 1
fi
PKG_VERSION="${AX_VERSION}.${AX_BUILD}"
echo "make-installer.sh: AX330G ${PKG_VERSION}"

# --- 1. Build Release (AU + VST3) into plugin-chain/build-release --------
if [[ ! -d "$BUILD_DIR" ]]; then
  cmake -B "$BUILD_DIR" -S "$PLUGIN_CHAIN" -G Xcode
fi
cmake --build "$BUILD_DIR" --config Release --target AX330G_AU AX330G_VST3

ARTEFACTS="$BUILD_DIR/AX330G_artefacts/Release"
COMPONENT="$ARTEFACTS/AU/AX330G.component"
VST3="$ARTEFACTS/VST3/AX330G.vst3"
[[ -d "$COMPONENT" ]] || { echo "make-installer.sh: missing $COMPONENT (build failed?)" >&2; exit 1; }
[[ -d "$VST3" ]] || { echo "make-installer.sh: missing $VST3 (build failed?)" >&2; exit 1; }

# --- 2. Stage a payload root ----------------------------------------------
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
STAGE="$WORK/root"
mkdir -p "$STAGE/Library/Audio/Plug-Ins/Components" "$STAGE/Library/Audio/Plug-Ins/VST3"
# ditto, not cp -R: preserves the bundles' code signature and xattrs exactly.
ditto "$COMPONENT" "$STAGE/Library/Audio/Plug-Ins/Components/AX330G.component"
ditto "$VST3" "$STAGE/Library/Audio/Plug-Ins/VST3/AX330G.vst3"

# --- 3. postinstall: clear the AU cache so the registrar sees the new build
SCRIPTS="$WORK/scripts"
mkdir -p "$SCRIPTS"
cat > "$SCRIPTS/postinstall" <<'EOF'
#!/bin/sh
killall -9 AudioComponentRegistrar 2>/dev/null || true
touch "/Library/Audio/Plug-Ins/Components/AX330G.component" 2>/dev/null || true
exit 0
EOF
chmod +x "$SCRIPTS/postinstall"

# --- 4. pkgbuild: component package ---------------------------------------
COMPONENT_PKG="$WORK/AX330G-component.pkg"
pkgbuild \
  --root "$STAGE" \
  --identifier "$PKG_IDENTIFIER" \
  --version "$PKG_VERSION" \
  --install-location / \
  --scripts "$SCRIPTS" \
  "$COMPONENT_PKG"

# --- 5. productbuild: distribution wrapper with welcome text -------------
RESOURCES="$WORK/resources"
mkdir -p "$RESOURCES"
cat > "$RESOURCES/welcome.html" <<EOF
<html><body style="font-family: -apple-system, sans-serif;">
<h2>AX330G ${PKG_VERSION}</h2>
<p>This installs the AX330G Audio Unit and VST3 plugin (a behavioral
emulation of the Korg AX30G/AX300G multi-effects) to:</p>
<ul>
<li>/Library/Audio/Plug-Ins/Components/AX330G.component</li>
<li>/Library/Audio/Plug-Ins/VST3/AX330G.vst3</li>
</ul>
<p><b>This installer is unsigned.</b> macOS Gatekeeper will refuse to open
it with a normal double-click. Instead, <b>right-click (Control-click) the
.pkg file and choose &ldquo;Open&rdquo;</b>, then confirm in the dialog
that appears. This is only needed the first time.</p>
</body></html>
EOF

cat > "$WORK/Distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="1">
    <title>AX330G</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <options customize="never" require-scripts="false" rootVolumeOnly="true"/>
    <choices-outline>
        <line choice="default">
            <line choice="${PKG_IDENTIFIER}"/>
        </line>
    </choices-outline>
    <choice id="default"/>
    <choice id="${PKG_IDENTIFIER}" visible="false">
        <pkg-ref id="${PKG_IDENTIFIER}"/>
    </choice>
    <pkg-ref id="${PKG_IDENTIFIER}" version="${PKG_VERSION}" onConclusion="none">AX330G-component.pkg</pkg-ref>
</installer-gui-script>
EOF

mkdir -p "$DIST_DIR"
OUT_PKG="$DIST_DIR/AX330G-${PKG_VERSION}.pkg"

# Sign with a Developer ID Installer identity if one exists; otherwise ship
# unsigned (expected — right-click/Open covers it, see the welcome text).
# (Two explicit productbuild calls, not a conditionally-populated array: an
# empty bash array under `set -u` throws "unbound variable" on macOS's
# stock bash 3.2 when expanded with "${arr[@]}".)
IDENTITY="$(security find-identity -v -p codesigning 2>/dev/null | grep 'Developer ID Installer' | head -1 | sed -E 's/^[^"]*"([^"]+)".*/\1/' || true)"
if [[ -n "$IDENTITY" ]]; then
  echo "make-installer.sh: signing with Developer ID Installer identity: $IDENTITY"
  productbuild \
    --distribution "$WORK/Distribution.xml" \
    --resources "$RESOURCES" \
    --package-path "$WORK" \
    --sign "$IDENTITY" \
    "$OUT_PKG"
else
  echo "make-installer.sh: no Developer ID Installer identity found — building unsigned"
  productbuild \
    --distribution "$WORK/Distribution.xml" \
    --resources "$RESOURCES" \
    --package-path "$WORK" \
    "$OUT_PKG"
fi

echo "make-installer.sh: wrote $OUT_PKG"
ls -la "$OUT_PKG"
