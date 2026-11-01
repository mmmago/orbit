<!-- SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux -->
<!-- SPDX-License-Identifier: MIT -->
<!-- Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md. -->

# Notice

Orbit is a modified version of **Deskflow**
(<https://github.com/deskflow/deskflow>), forked from Deskflow's `master`
branch at commit `8bbdefd` (October 2026).

- Deskflow: Copyright (C) 2024-2026 Deskflow Developers
- Synergy: Copyright (C) 2012-2025 Symless Ltd / Synergy App Ltd
- Copyright (C) 2009-2012 Nick Bolton
- Copyright (C) 2002-2009 Chris Schoeneman

Orbit changes: Copyright (C) 2026 Lucas Magotteaux.

Orbit is distributed under the GNU General Public License, version 2 only,
with the OpenSSL exception (see `LICENSE` and `LICENSES/`). Files keep their
original copyright and license headers. Modified files carry dated Orbit
change notices; new Orbit files identify their author and license. The full
history of changes is in this repository's git log.

"Deskflow" is the name of the original project. Orbit is not affiliated with
or endorsed by it.

## Changes from Deskflow

October-November 2026, Lucas Magotteaux:

- **Renamed to Orbit**: new name, application id (`io.github.mmmago.orbit`),
  icons and links. The Deskflow update check is removed.
- **macOS only**: the build stops on other platforms. The Windows and Linux
  code is kept unchanged but is not built or supported.
- **File and folder drag and drop between Macs** (protocol 1.9), with a
  helper process, Finder progress and a copy window.
- **Trackpad gestures forwarded to macOS clients** (protocol 1.10): Dock
  swipes, pinch, rotate, phased trackpad scrolling and the Accessibility Zoom
  gesture, with a setting to turn them off.
- macOS clipboard monitor and cursor-hiding fixes.
- Developer tools for drag and drop and gestures (`tools/`).
- Native SwiftUI menu bar panel, About page, and GitHub release downloads
  with verified installation and restart.
- Release packaging includes dependency licenses, notices and matching source
  archives, with checks against the shipped library inventory.

Third-party libraries and artwork retain their respective licenses and
ownership; see `THIRD_PARTY_NOTICES.md` and `third-party/macos-arm64/`.
The macOS app includes these documents in `Contents/Resources/Legal/`.

The Buy Me a Coffee button in About uses the service's official yellow
button image (<https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png>)
and links to the Orbit maintainer's support page. The Buy Me a Coffee name
and artwork belong to their respective owners.
