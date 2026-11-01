<!-- SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux -->
<!-- SPDX-License-Identifier: MIT -->
<!-- Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md. -->

# Installing Orbit on a Mac

1. Package: `tools/filedrag/package-macos.sh build ../dist-orbit "<codesign identity>" /path/to/source-cache`
   (this empties `../dist-orbit` first).
   Prepare the verified dependency source cache using
   [source distribution](../../docs/source-distribution.md); packaging also
   emits the matching source archive for the release.
2. `install-orbit.sh <Orbit.zip> <sha256 of Orbit.app/Contents/MacOS/orbit-core>`
   puts Orbit.app in /Applications and, the first time, copies the Deskflow
   settings and certificates from ~/Library/Deskflow to ~/Library/Orbit.
   Quit Orbit first when updating it.
3. `switch-to-orbit.sh` (once, when replacing Deskflow): quits Deskflow,
   moves it to ~/Deskflow-backups and starts Orbit.
4. `update-orbit.sh <Orbit.zip> <sha256>`: quit Orbit, install a new build, start it again.
