#!/bin/bash
# SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
# SPDX-License-Identifier: MIT
# Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
# Quit Deskflow, move it out of /Applications (kept in ~/Deskflow-backups), start Orbit.
set -euo pipefail
BACKUPS="$HOME/Deskflow-backups"
if pgrep -x Deskflow >/dev/null; then
  osascript -e 'with timeout of 15 seconds' -e 'tell application "Deskflow" to quit' -e 'end timeout' 2>/dev/null || true
fi
for ((i=0; i<40; ++i)); do
  pgrep -x Deskflow >/dev/null || pgrep -x deskflow-core >/dev/null || break
  sleep 0.25
done
for P in Deskflow deskflow-core deskflow-drag-helper; do pkill -TERM -x "$P" 2>/dev/null || true; done
for ((i=0; i<40; ++i)); do
  pgrep -x Deskflow >/dev/null || pgrep -x deskflow-core >/dev/null || pgrep -x deskflow-drag-helper >/dev/null || break
  sleep 0.25
done
if pgrep -x Deskflow >/dev/null || pgrep -x deskflow-core >/dev/null; then
  echo 'Deskflow did not stop; nothing changed.' >&2
  exit 1
fi
mkdir -p "$BACKUPS"
if test -d /Applications/Deskflow.app; then
  mv /Applications/Deskflow.app "$BACKUPS/Deskflow-modded-$(date +%Y%m%d-%H%M).app"
fi
open -a /Applications/Orbit.app
echo "Deskflow moved to $BACKUPS; Orbit started"
