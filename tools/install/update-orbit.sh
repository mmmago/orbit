#!/bin/bash
# SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
# SPDX-License-Identifier: MIT
# Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
# Update an installed Orbit: quit it, install the new build, start it again.
# usage: update-orbit.sh <Orbit.zip> <sha256 of Orbit.app/Contents/MacOS/orbit-core>
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
if pgrep -x Orbit >/dev/null; then
  osascript -e 'with timeout of 15 seconds' -e 'tell application "Orbit" to quit' -e 'end timeout' 2>/dev/null || true
fi
for ((i=0; i<40; ++i)); do
  pgrep -x Orbit >/dev/null || pgrep -x orbit-core >/dev/null || break
  sleep 0.25
done
for P in Orbit orbit-core orbit-drag-helper; do pkill -TERM -x "$P" 2>/dev/null || true; done
sleep 1
"$HERE/install-orbit.sh" "$1" "$2"
open -a /Applications/Orbit.app
for ((i=0; i<60; ++i)); do pgrep -x orbit-core >/dev/null && break; sleep 0.5; done
pgrep -x orbit-core >/dev/null && echo "Orbit updated and running" || echo "Orbit installed; it has not started yet" >&2
