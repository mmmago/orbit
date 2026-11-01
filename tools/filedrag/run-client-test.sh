#!/bin/bash
# SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
# SPDX-License-Identifier: MIT
# Run a second, test-only deskflow-core client on this Mac against fake_server.py.
# It uses its own settings (TLS off, port 24991) and its own TMPDIR so it does not
# touch a Deskflow instance that is already running.
#
# usage: run-client-test.sh <build-dir> <work-dir> <label> <fake_server args...>
set -u
BUILD=$1 WORK=$2 LABEL=$3; shift 3
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$WORK/ipc"
cat > "$WORK/test.conf" <<CONF
[core]
computerName=dndtest
port=24991

[client]
remoteHost=127.0.0.1

[security]
tlsEnabled=false

[log]
level=DEBUG
toFile=false
CONF
python3 -I "$HERE/fake_server.py" --port 24991 "$@" > "$WORK/server-$LABEL.log" 2>&1 &
SP=$!
sleep 0.5
TMPDIR="$WORK/ipc" "$BUILD/bin/Deskflow.app/Contents/MacOS/deskflow-core" client --new-instance \
  -s "$WORK/test.conf" > "$WORK/client-$LABEL.log" 2>&1 &
CP=$!
wait $SP
sleep 1
kill $CP 2>/dev/null
wait $CP 2>/dev/null
echo "=== server"; cat "$WORK/server-$LABEL.log"
echo "=== client"; grep -iE "file drag|warn|err" "$WORK/client-$LABEL.log" | grep -v "cursor may not\|ipc server failed\|failed to connect"
