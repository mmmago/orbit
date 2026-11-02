<!-- SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux -->
<!-- SPDX-License-Identifier: MIT -->
<!-- Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md. -->

<p align="center"><img src="https://raw.githubusercontent.com/mmmago/orbit/21e002ba52d6fc5a25828626efaea8398a143e5f/src/apps/res/orbit-appicon.svg" width="128" alt="Orbit icon"></p>

# Orbit

One keyboard, mouse and trackpad for several Macs, with drag and drop of
files between them, like Universal Control.

Orbit is macOS only and based on [Deskflow](https://github.com/deskflow/deskflow)
(GPL-2.0). It is not affiliated with the Deskflow project: report Orbit
problems here.

## Features

- **Drag and drop**: drag files and folders from one Mac to the other and
  drop them anywhere. They show up at once with a Finder-style progress, and
  several transfers can run together.
- **Trackpad gestures**: swipes (Spaces, Mission Control), pinch, rotate,
  smooth scrolling and swiping between pages work on the other Mac, as does
  the Accessibility Zoom gesture.
- **Clipboard**: copy text, HTML or images on one Mac, paste on the other.
- **Touch ID unlock**: unlock a locked client with Touch ID on the server.
- Everything goes over one TLS connection on your network.

## Install

Download `Orbit-macos-arm64.zip` from
[Releases](https://github.com/mmmago/orbit/releases) and put `Orbit.app` in
Applications on every Mac. Pick one Mac as the server, the others as clients.
macOS asks for **Accessibility** permission; if it blocks the first launch,
allow it in System Settings → Privacy & Security.

Orbit checks for new releases and asks before installing them (**About
Orbit**).

## Settings

The options are in the Orbit panel (menu bar icon): trackpad gestures, drag
and drop and clipboard on the server, scroll direction and Touch ID unlock on
the client. They are saved in `~/Library/Orbit/Orbit.conf`.

## License

GNU General Public License, version 2 only, with the OpenSSL exception, as
Deskflow ([LICENSE](LICENSE), [LICENSES](LICENSES)). Each release includes
`Orbit-sources-macos-arm64.tar.gz` with the matching source of Orbit and its
bundled libraries ([source distribution](docs/source-distribution.md),
[third-party notices](THIRD_PARTY_NOTICES.md)). Changes from Deskflow are
listed in [NOTICE.md](NOTICE.md).
