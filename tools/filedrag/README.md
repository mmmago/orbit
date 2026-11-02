<!-- SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux -->
<!-- SPDX-License-Identifier: MIT -->
<!-- Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md. -->

# File drag and drop: developer tools

Tools used to build, package and test the file drag and drop feature on
macOS (see [docs/drag-and-drop-design.md](../../docs/drag-and-drop-design.md)).
None of this is part of the shipped app.

## Packaging

`package-macos.sh <build-dir> <out-dir> "<codesign identity>"` turns a CMake
build into a self-contained `Deskflow.app` (Qt bundled with `macdeployqt`,
QtSvg added by hand because Homebrew ships it in its own keg), signs it and
zips it. With an *Apple Development* identity the app runs on your own Macs;
public releases need a *Developer ID Application* identity and notarization.

Packaging a release needs a clean, `v*`-tagged tree and the dependency source
cache, and bundles the legal documents and source archive. To package any build
for testing on your own Macs, put `--dev` first: it skips that step.

## End-to-end tests on one Mac

`fake_server.py` speaks enough of the Deskflow protocol (1.9, no TLS) to drive
a real `deskflow-core` client:

- default: announce files, enter the client screen, stream the data with the
  same ack window as the real server, move the cursor and release (drop);
- `--manifest-delay 3`: start folder promises immediately and delay the full
  file list by three seconds (drop while the source is still scanning);
- `--chunk-delay`: simulate a slow link (drop before the data arrived);
- `--release-early`: release over nothing (cancel path);
- `--key-after-drop KEYID`: press a key after the drop;
- `--second FILE...`: start a second drag while the first transfer runs;
- `--reverse OUT_DIR`: client → server: press on an item, drag, wait for the
  client's drag state, ask it to hand over and store the received files.

`run-client-test.sh <build-dir> <work-dir> <label> <fake_server args>` starts
the fake server and a separate test client (own settings, own `TMPDIR`, port
24991) so an installed Deskflow keeps running.

These tests move the real cursor. `waitidle.sh 15` waits until nobody touched
the keyboard or mouse for 15 seconds first. The process that runs them needs
Accessibility permission.

### Helpers (`test-helpers/`, compile with `swiftc -O file.swift -o name`)

| Tool | Purpose |
|------|---------|
| `finderwin` | open a folder in Finder and place its window |
| `findrow` | find an item in the front Finder window, print its center |
| `droptarget` | a window that logs every drag callback and accepts drops |
| `helperharness` | drive `deskflow-drag-helper` like a server does (begin, pressAt, press, drag, drop) |
| `cancelprogress` | cancel published file progress like Finder's cancel button |
| `presscancel` | press a stop button in the copy window via Accessibility |
| `frontwatch` | log frontmost-app changes (focus must stay with the drop target) |
| `allwins`, `winid` | list windows / find a Finder window id (for `screencapture -l`) |
| `moveto`, `btnstate`, `closewin`, `axcheck` | move the cursor, print button state, close a Finder window, check permissions |

## Spikes (`spikes/`)

The two throwaway experiments that validated the approach before the feature
was built: `receiver` (file-promise drag from injected events) and `source`
(detect a Finder file drag and cancel it). Results are in the design doc.
