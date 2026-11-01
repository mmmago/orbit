#!/bin/bash
# SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
# SPDX-License-Identifier: MIT
# Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
# Launched only after a verified update and the user's Install and Restart.
# All paths are arguments, never interpolated into shell source.
set -eu
parent_pid=$1
destination=$2
work=$3
backup="$work/Previous.app"
replacement="$work/Orbit.app"

# Allow the normal quit path to stop the core and file-drag helper first.
for ((attempt=0; attempt<120; ++attempt)); do
  kill -0 "$parent_pid" 2>/dev/null || break
  /bin/sleep 0.25
done
if kill -0 "$parent_pid" 2>/dev/null; then
  /bin/rm -rf "$work"
  exit 1
fi

failed() {
  /usr/bin/open "$destination" || true
  /usr/bin/osascript -e 'display alert "Orbit could not install the update" message "Your previous copy has been kept. Download the latest release from github.com/mmmago/orbit/releases and install it manually." as warning' || true
  exit 1
}

/bin/mv "$destination" "$backup" || failed
if ! /bin/mv "$replacement" "$destination"; then
  /bin/mv "$backup" "$destination" || true
  failed
fi
if ! /usr/bin/open "$destination"; then
  /bin/mv "$destination" "$replacement"
  /bin/mv "$backup" "$destination"
  failed
fi
/bin/rm -rf "$work"
