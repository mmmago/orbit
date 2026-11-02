#!/bin/bash
# SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
# SPDX-License-Identifier: MIT
# Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
# Build a self-contained, development-signed Orbit.app (Qt bundled) and zip it.
# usage: package-macos.sh [--dev] <build-dir> <out-dir> "<codesign identity>" [source-cache]
#
# Without --dev this packages a release: the tree must be clean and HEAD tagged
# v*, and the legal documents and matching source archive are bundled.
# --dev packages any build for testing on your own Macs: no legal documents, no
# source archive, nothing to publish.
set -euo pipefail
DEV=0
if [ "${1:-}" = "--dev" ]; then
  DEV=1
  shift
fi
BUILD=$1
OUT=$2
ID=$3
SOURCE_CACHE=${4:-"$BUILD/compliance-sources"}
COMPLIANCE="$(dirname "$0")/../compliance/macos.py"
DEPLOYQT=$(ls /opt/homebrew/Cellar/qtbase/*/bin/macdeployqt | tail -1)
APP="$OUT/Orbit.app"

rm -rf "$OUT"
cmake --install "$BUILD" --prefix "$OUT" > "$OUT.install.log" 2>&1
"$DEPLOYQT" "$APP" "-executable=$APP/Contents/MacOS/orbit-core" -hardened-runtime "-codesign=$ID" > "$OUT.deployqt.log" 2>&1

# Homebrew ships QtSvg in its own keg; macdeployqt misses it (needed by the svg plugins)
SVG="$APP/Contents/Frameworks/QtSvg.framework"
ditto /opt/homebrew/opt/qtsvg/lib/QtSvg.framework "$SVG"
rm -rf "$SVG/Headers" "$SVG/Versions/A/Headers"
install_name_tool -id @rpath/QtSvg.framework/Versions/A/QtSvg \
  -change /opt/homebrew/opt/qtbase/lib/QtGui.framework/Versions/A/QtGui @rpath/QtGui.framework/Versions/A/QtGui \
  -change /opt/homebrew/opt/qtbase/lib/QtCore.framework/Versions/A/QtCore @rpath/QtCore.framework/Versions/A/QtCore \
  "$SVG/Versions/A/QtSvg" 2>/dev/null

# The linker's debug map records the absolute path of every object file, i.e.
# the builder's home directory. Drop it before the binaries are (re)signed below.
for bin in Orbit orbit-core orbit-drag-helper; do
  strip -S "$APP/Contents/MacOS/$bin"
done

# Verify every shipped library and install legal/source-access documents before
# sealing the app. The matching source archive is emitted alongside Orbit.zip.
if [ "$DEV" = 1 ]; then
  echo "dev package: no legal documents or source archive, not for release" >&2
else
  python3 "$COMPLIANCE" bundle --app "$APP" --cache "$SOURCE_CACHE" --output "$OUT"
fi

codesign --force --sign "$ID" --options runtime --timestamp=none "$SVG"
codesign --force --sign "$ID" --options runtime --timestamp=none --preserve-metadata=entitlements "$APP/Contents/MacOS/orbit-core"
codesign --force --sign "$ID" --options runtime --timestamp=none "$APP/Contents/MacOS/orbit-drag-helper"
codesign --force --sign "$ID" --options runtime --timestamp=none --preserve-metadata=entitlements "$APP"
codesign --verify --deep --strict "$APP"

if find "$APP" -type f -perm +111 -exec otool -L {} \; 2>/dev/null | grep -qE "/opt/homebrew|/usr/local"; then
  echo "error: bundle still references Homebrew libraries" >&2
  exit 1
fi

(cd "$OUT" && ditto -c -k --keepParent Orbit.app Orbit-macos-arm64.zip)
# Keep the historical local installer name, with the release name in checksums.
cp "$OUT/Orbit-macos-arm64.zip" "$OUT/Orbit.zip"
if [ "$DEV" = 1 ]; then
  (cd "$OUT" && shasum -a 256 Orbit-macos-arm64.zip > SHA256SUMS)
else
  (cd "$OUT" && shasum -a 256 Orbit-macos-arm64.zip Orbit-sources-macos-arm64.tar.gz > SHA256SUMS)
fi
echo "$OUT/Orbit.zip"
