#!/bin/bash
# SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
# SPDX-License-Identifier: MIT
# Exercise replacement/rollback with fake apps and launch/move commands.
# Never launch or modify a real app.
set -euo pipefail
source_helper=$1
test_root=$(mktemp -d /tmp/orbit-installer-tests.XXXXXX)
trap '/bin/rm -rf "$test_root"' EXIT
export ORBIT_INSTALL_TEST_ROOT="$test_root"

cat > "$test_root/open" <<'SCRIPT'
#!/bin/bash
set -eu
marker=$(cat "$1/marker")
echo "$marker" >> "$ORBIT_INSTALL_TEST_ROOT/opened"
if test -f "$ORBIT_INSTALL_TEST_ROOT/fail-open" && test "$marker" = new; then exit 1; fi
SCRIPT
cat > "$test_root/mv" <<'SCRIPT'
#!/bin/bash
set -eu
if test -f "$ORBIT_INSTALL_TEST_ROOT/fail-move" && test "$1" = "$ORBIT_INSTALL_TEST_ROOT/Update work/Orbit.app"; then exit 1; fi
exec /bin/mv "$@"
SCRIPT
chmod +x "$test_root/open" "$test_root/mv"
sed -e "s|/usr/bin/open|$test_root/open|g" \
    -e "s|/bin/mv|$test_root/mv|g" \
    -e 's|/usr/bin/osascript|/usr/bin/true|g' \
    -e 's|/bin/sleep 0.25|/usr/bin/true|g' \
    "$source_helper" > "$test_root/helper.sh"

prepare() {
  rm -rf "$test_root/Current Orbit.app" "$test_root/Update work"
  rm -f "$test_root/fail-move" "$test_root/fail-open" "$test_root/opened"
  mkdir -p "$test_root/Current Orbit.app" "$test_root/Update work/Orbit.app"
  echo old > "$test_root/Current Orbit.app/marker"
  echo new > "$test_root/Update work/Orbit.app/marker"
}

install() {
  /bin/bash "$test_root/helper.sh" "${1:-2147483647}" "$test_root/Current Orbit.app" "$test_root/Update work"
}

prepare
install
test "$(cat "$test_root/Current Orbit.app/marker")" = new
test "$(cat "$test_root/opened")" = new
test ! -d "$test_root/Update work"

prepare
touch "$test_root/fail-move"
if install; then echo 'Expected replacement failure' >&2; exit 1; fi
test "$(cat "$test_root/Current Orbit.app/marker")" = old
test "$(cat "$test_root/opened")" = old

prepare
touch "$test_root/fail-open"
if install; then echo 'Expected launch failure' >&2; exit 1; fi
test "$(cat "$test_root/Current Orbit.app/marker")" = old
test "$(tail -1 "$test_root/opened")" = old

prepare
if install "$$"; then echo 'Expected quit timeout' >&2; exit 1; fi
test "$(cat "$test_root/Current Orbit.app/marker")" = old
test ! -f "$test_root/opened"

echo 'InstallUpdateTests passed: replacement, move rollback, launch rollback, quit timeout, paths with spaces.'
