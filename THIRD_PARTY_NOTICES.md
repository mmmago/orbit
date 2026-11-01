<!-- SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux -->
<!-- SPDX-License-Identifier: MIT -->
<!-- Orbit file added: 2026-11-01 by Lucas Magotteaux. -->

# Third-party notices

Orbit is a modified version of Deskflow. Its combined application code is
distributed under GPL-2.0-only with the OpenSSL linking exception. See `LICENSE`,
`LICENSES/LicenseRef-OpenSSL-Exception.txt` and `NOTICE.md`. There is no warranty.
Original author notices are preserved in the source files.

## Libraries in the Apple Silicon macOS release

The versions below identify the upstream source. Homebrew revisions, exact
shipped binary paths/build UUIDs, upstream source URLs and SHA-256 checksums are
recorded in `third-party/macos-arm64/manifest.json`. The matching upstream
copyright notices and complete license texts are preserved, without rewriting,
under each component's `notices/` directory there.
Where a notice lives in a source header (xxHash), the verbatim opening legal
comment is included as `xxhash.h.LICENSE.txt`; the full source file remains in
the corresponding source archive.

| Component | Version | License for the shipped library |
| --- | --- | --- |
| Brotli | 1.2.1 | MIT |
| D-Bus | 1.16.2 | GPL-2.0-or-later option |
| double-conversion | 3.4.0 | BSD-3-Clause |
| FreeType | 2.15.0 | FreeType Project License (FTL) |
| GNU gettext `libintl` | 1.0 | LGPL-2.1-or-later; GPL command-line tools are not shipped |
| GLib, GObject, GIO, GModule, GThread | 2.90.1 | LGPL-2.1-or-later |
| Graphite2 | 1.3.15 | LGPL-2.1-or-later option |
| HarfBuzz | 14.6.0 | MIT; see its bundled notices for individual contributions |
| ICU | 78.3 | ICU/Unicode licenses in its license files |
| libjpeg-turbo | 3.2.0 | IJG, Zlib and BSD-3-Clause |
| libb2 | 0.98.1 | CC0-1.0 |
| libpng | 1.6.59 | libpng license |
| MD4C | 0.6.0 | MIT |
| OpenSSL | 3.6.4 and 4.1.0 | Apache-2.0; linked using the Deskflow OpenSSL exception |
| PCRE2 | 10.49 | BSD-3-Clause and the notices included with PCRE2 |
| Qt Core, GUI, Widgets, Network, D-Bus and platform/image/TLS plug-ins | 6.12.0 | GPL-2.0 option; third-party portions retain their own notices |
| Qt SVG and its plug-ins | 6.12.0 | GPL-2.0 option |
| Zstandard | 1.5.7 | BSD-3-Clause option, plus BSD-2-Clause/MIT notices for applicable portions |

Qt's `qt_attribution.json` files and upstream `LICENSES/` texts are included with
the captured notices. This collection deliberately preserves notices for the
upstream source packages, including optional code not present in this build.
The binary inventory, rather than the presence of a notice file, identifies
what is actually shipped. Similarly, Homebrew SBOMs may describe build tools,
documentation, and dependencies from the bottle build; their package-wide
license expressions do not replace the runtime component licenses above.

The Homebrew build recipes/helper scripts retain their BSD-2-Clause license;
see `third-party/macos-arm64/LICENSE-Homebrew.txt`. Formulae are preserved as
build scripts, including inline patches and configuration edits.

## Images and icons

- Inherited KDE Breeze action/device/place/status icons are LGPL-2.1-only.
  Their original attribution and license are recorded in `REUSE.toml` and
  `LICENSES/LGPL-2.1-only.txt`; their editable SVG source is included in Orbit's
  source archive. Theme index attribution remains with Chris Rizzitello.
- Deskflow's mark and wordmark belong to the Deskflow developers and retain
  GPL-2.0-only terms. Displaying them acknowledges the upstream project and
  does not imply endorsement.
- Orbit's own icon artwork is attributed in its source SVG headers.
- The optional Buy Me a Coffee button uses the service's official promotional
  button image, obtained from
  <https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png>. The service
  provides promotional buttons through <https://buymeacoffee.com/brand>.
  Its name and artwork remain with their respective owners; no ownership of
  those marks or endorsement is claimed.

## Source access and your rights

Every binary release is accompanied, on the same GitHub release page, by
`Orbit-sources-macos-arm64.tar.gz`. It contains Orbit's matching source and
build/install scripts, all bundled libraries' upstream source archives,
applicable Homebrew patches/resources/recipes, and the Homebrew build helper
source. The source inventory includes checksums so recipients can verify it.

The release app also includes the matching download link and source archive
checksum in `Contents/Resources/Legal/SOURCE_CODE.md`. See
`docs/source-distribution.md` for the layout and build instructions. Redistribution
must preserve the applicable license terms, notices and corresponding-source
access. You may modify the libraries and application under their licenses;
these notices impose no additional restrictions on those rights.
