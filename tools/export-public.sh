#!/bin/bash
# Copies the PUBLIC parts of this project into the neglectware/ax330g git
# repository (default ~/Developer/neglectware/ax330g). This folder stays the
# working copy; the public repo is generated from it by an ALLOW list, so a
# new private file never reaches GitHub by accident.
#
# Not exported, on purpose: captures/ and out/ (recordings, gigabytes),
# docs/ except docs/manual/ (internal measurement notes), manual/ (Korg's
# owner's and service manuals -- their copyright), AX30G_HANDOFF.md and
# README.md (internal; the repo has its own README), plugin/ (the retired
# single-effect test plugin), dist/, videos, build products.
#
#   tools/export-public.sh [repo-dir]      then review, commit, push in repo-dir
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${1:-$HOME/Developer/neglectware/ax330g}"
mkdir -p "$DEST"

RS=(rsync -a --delete --prune-empty-dirs
    --exclude '__pycache__/' --exclude '*.pyc' --exclude '.DS_Store'
    --exclude 'build*/' --exclude '*.log' --exclude '*.npy')

"${RS[@]}" "$HERE/dsp/"      "$DEST/dsp/"
"${RS[@]}" "$HERE/engine/"   "$DEST/engine/"
"${RS[@]}" "$HERE/analysis/" "$DEST/analysis/"
"${RS[@]}" "$HERE/tests/"    "$DEST/tests/"
"${RS[@]}" "$HERE/models/"   "$DEST/models/"
"${RS[@]}" --exclude '*.wav' "$HERE/capture/" "$DEST/capture/"          # signal sets are generated: make signals-normal
"${RS[@]}" --include 'CMakeLists.txt' --include 'src/***' --include 'tools/***' --include 'fonts/***' --exclude '*' \
           "$HERE/plugin-chain/" "$DEST/plugin-chain/"
"${RS[@]}" --include 'Makefile' --include 'src/***' --include 'tools/***' --exclude '*' \
           "$HERE/app/" "$DEST/app/"
"${RS[@]}" --exclude 'ax30g-render' "$HERE/tools/render/"      "$DEST/tools/render/"
"${RS[@]}" --exclude 'pluginrender' "$HERE/tools/pluginrender/" "$DEST/tools/pluginrender/"
"${RS[@]}" --exclude 'samp3.py' "$HERE/tools/lcd/"             "$DEST/tools/lcd/"
cp "$HERE/tools/make-installer.sh" "$HERE/tools/export-public.sh" "$DEST/tools/"
cp "$HERE/Makefile" "$DEST/Makefile"
"${RS[@]}" "$HERE/docs/manual/" "$DEST/docs/manual/"
# The public README, LICENSE, .gitignore and the Windows build's install
# note live in public/ here and are copied to the repo root.
cp "$HERE/public/README.md" "$HERE/public/LICENSE" "$HERE/public/.gitignore" "$HERE/public/INSTALL-Windows.txt" "$DEST/"
# The GitHub Actions workflow(s) also live under public/ here (CI config is
# only meaningful in the public repo) and are copied to the repo's own
# .github/ tree.
"${RS[@]}" "$HERE/public/.github/" "$DEST/.github/"

# Refuse to finish if anything private slipped into the export: home paths,
# the brain folder, people's names, old identifiers, scratch paths.
if HITS=$(grep -rIl -i -E -f "$HERE/private/export-deny-patterns.txt" "$DEST" --exclude-dir=.git); then
  echo "export-public.sh: PRIVATE DETAILS FOUND -- do not commit:" >&2
  echo "$HITS" >&2
  exit 1
fi
echo "exported to $DEST"
