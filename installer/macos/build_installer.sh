#!/usr/bin/env bash
# Builds a macOS .pkg installer for Monarch of Tone from already-built Release artefacts, with a
# choose-your-format screen (AU / VST3, both selected by default — see Distribution.xml).
#
# Usage:
#   installer/macos/build_installer.sh <version> [artefacts-dir] [output-dir]
#
#   <version>        e.g. 0.8.0 — matches CMakeLists.txt's project() version
#   [artefacts-dir]  defaults to build/Monarch_artefacts/Release (repo-root relative)
#   [output-dir]     defaults to the current directory
#
# Requires the AU and VST3 targets to already be built (Monarch_AU, Monarch_VST3).
# The resulting .pkg is unsigned — sign it separately with productsign if distributing
# outside this machine (the AU/VST3 bundles it wraps are already signed/notarized by
# release.yml before this script runs).

set -euo pipefail

VERSION="${1:?Usage: build_installer.sh <version> [artefacts-dir] [output-dir]}"
ARTEFACTS="${2:-build/Monarch_artefacts/Release}"
OUTDIR="${3:-.}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

AU_SRC="$ARTEFACTS/AU/Monarch of Tone.component"
VST3_SRC="$ARTEFACTS/VST3/Monarch of Tone.vst3"

[ -d "$AU_SRC" ] || { echo "error: $AU_SRC not found — build Monarch_AU first" >&2; exit 1; }
[ -d "$VST3_SRC" ] || { echo "error: $VST3_SRC not found — build Monarch_VST3 first" >&2; exit 1; }

AU_ROOT="$WORK/au-root"
VST3_ROOT="$WORK/vst3-root"
mkdir -p "$AU_ROOT" "$VST3_ROOT"
cp -R "$AU_SRC" "$AU_ROOT/"
cp -R "$VST3_SRC" "$VST3_ROOT/"

pkgbuild --root "$AU_ROOT" \
    --identifier com.leighpierce.monarchoftone.au \
    --version "$VERSION" \
    --install-location "/Library/Audio/Plug-Ins/Components" \
    "$WORK/au.pkg"

pkgbuild --root "$VST3_ROOT" \
    --identifier com.leighpierce.monarchoftone.vst3 \
    --version "$VERSION" \
    --install-location "/Library/Audio/Plug-Ins/VST3" \
    "$WORK/vst3.pkg"

sed "s/__VERSION__/$VERSION/g" "$SCRIPT_DIR/Distribution.xml" > "$WORK/Distribution.xml"

mkdir -p "$OUTDIR"
OUT_PKG="$OUTDIR/Monarch-of-Tone-${VERSION}-macOS-Installer.pkg"

productbuild --distribution "$WORK/Distribution.xml" \
    --package-path "$WORK" \
    "$OUT_PKG"

echo "Built $OUT_PKG"
