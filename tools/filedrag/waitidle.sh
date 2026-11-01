#!/bin/bash
# SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
# SPDX-License-Identifier: MIT
# Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
# Wait until there was no keyboard/mouse input for $1 seconds (default 15, max 10 min),
# so end-to-end tests that move the cursor don't collide with someone using the Mac.
need=${1:-15}
for _ in $(seq 1 300); do
  idle=$(ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}')
  [ "$idle" -ge "$need" ] && { echo "idle ${idle}s, starting"; exit 0; }
  sleep 2
done
echo "user still active, giving up"
exit 1
