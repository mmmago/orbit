#!/bin/bash
# SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
# SPDX-License-Identifier: MIT
# Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
# Stage Orbit.app in /Applications and copy Deskflow's settings to Orbit.
# Deskflow keeps running; switch-to-orbit.sh does the swap.
# usage: install-orbit.sh <Orbit.zip> <expected orbit-core sha256>
set -euo pipefail
ZIP=$1
EXPECTED_CORE=$2
WORK=$(mktemp -d /tmp/orbit-install.XXXXXX)

ditto -x -k "$ZIP" "$WORK"
codesign --verify --deep --strict "$WORK/Orbit.app"
test "$(shasum -a 256 "$WORK/Orbit.app/Contents/MacOS/orbit-core" | awk '{print $1}')" = "$EXPECTED_CORE"
if test -d /Applications/Orbit.app; then mv /Applications/Orbit.app "$WORK/Orbit-old.app"; fi
ditto "$WORK/Orbit.app" /Applications/Orbit.app
codesign --verify --deep --strict /Applications/Orbit.app

OLD="$HOME/Library/Deskflow"
NEW="$HOME/Library/Orbit"
if test -d "$NEW"; then
  echo "Orbit settings already exist, left unchanged: $NEW"
else
  mkdir -p "$NEW/tls"
  sed -e 's|/Library/Deskflow/|/Library/Orbit/|g' \
      -e 's|tls/deskflow\.pem|tls/orbit.pem|g' \
      -e 's|deskflow-server\.conf|orbit-server.conf|g' \
      -e 's|Logs/deskflow\.log|Logs/orbit.log|g' \
      "$OLD/Deskflow.conf" > "$NEW/Orbit.conf"
  test -f "$OLD/deskflow-server.conf" && cp -p "$OLD/deskflow-server.conf" "$NEW/orbit-server.conf"
  cp -p "$OLD/tls/deskflow.pem" "$NEW/tls/orbit.pem"
  chmod 600 "$NEW/tls/orbit.pem"
  for f in trusted-clients trusted-servers; do test -e "$OLD/tls/$f" && cp -pR "$OLD/tls/$f" "$NEW/tls/$f"; done
  echo "Settings copied to $NEW"
fi
echo "Orbit.app staged; remaining Deskflow references in Orbit.conf:"
grep -n -i "deskflow" "$NEW/Orbit.conf" || echo "  none"
