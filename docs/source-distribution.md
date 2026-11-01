<!-- SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux -->
<!-- SPDX-License-Identifier: MIT -->
<!-- Orbit file added: 2026-11-01 by Lucas Magotteaux. -->

# Source code accompanying Orbit releases

On the [same release page as the app](https://github.com/mmmago/orbit/releases),
download **Orbit-sources-macos-arm64.tar.gz**. This is the corresponding-source
distribution accompanying the Apple Silicon release, rather than a promise to
obtain source from an unrelated upstream server later. The GitHub-generated
source ZIP alone does not contain the bundled libraries' source.

The application is GPL-2.0-only with its OpenSSL exception. Third-party files
retain their own compatible license terms. These are free-software licenses:
there is no warranty and you may modify and redistribute under their terms.
Read the included license texts for the full conditions.

## Archive layout

Extract the outer archive with `tar -xzf Orbit-sources-macos-arm64.tar.gz`.
The `Orbit-sources-macos-arm64/` directory contains:

- `release.json`: the exact release tag and Orbit commit used for this build.
- `Orbit.tar.gz`: that commit's complete tracked source tree, including SVG
  icon source and all compilation, packaging and installation scripts.
- `downloads/COMPONENT/`: checksum-verified original upstream source archives,
  external patches and formula resources. They remain in their original
  archive format to preserve their contents exactly.
- `third-party/manifest.json`: component versions, source/patch SHA-256 values,
  Homebrew revisions, and build UUIDs for every distributed library/plug-in.
- `third-party/COMPONENT/COMPONENT.rb`: the installed Homebrew recipe, including
  its build configuration, inline `DATA` patches and `inreplace` source edits.
- `third-party/COMPONENT/notices/`: verbatim upstream legal notices.
- `Homebrew-build-scripts.tar.gz`: the Homebrew source revision identified in
  the manifest, including the recipe helper implementations and its license.

Homebrew receipt paths and the maintainer's private build cache are not required
and are not included. The manifest's library paths are relative to the Homebrew
Cellar, not to a maintainer home directory.

## Rebuilding

Unpack `Orbit.tar.gz`. The application build commands are in its `README.md`;
they use Xcode's C++/Swift toolchains, CMake, Ninja, Qt and OpenSSL on macOS.
No Apple signing private key is part of the source or needed to compile it.
For a local build, omit the development-signing option or use your own identity.
The runtime libraries remain dynamically linked and can be rebuilt/replaced;
modified app bundles must be signed again with your own identity or ad-hoc
signature. Do not expect the original publisher's signature to remain valid.

To rebuild a bundled dependency, unpack its archive in `downloads/`, apply its
listed external patches and any recipe `DATA` patch, and follow the archived
recipe's `install` steps. Preserve `inreplace` edits and platform-specific
options, adapting the installation prefix to your own machine. The upstream
archives also contain their own CMake/Meson/configure build systems and
interface definitions. Formula resources are included for recipes that use
them. Homebrew helper source is provided for `std_cmake_args`, `std_meson_args`
and the other recipe helpers.

You can use a local Homebrew tap for the archived recipes: preserve the patch
paths and point source/resource URLs at the locally extracted downloads (keeping
their SHA-256 checks). Install matching dependency versions before building Qt,
and select the resulting Qt/OpenSSL prefixes with `CMAKE_PREFIX_PATH` and
`OPENSSL_ROOT_DIR` as shown in Orbit's README. Current Homebrew packages are also
suitable for development, but do not claim to be the exact release dependencies.
The full preferred source and build scripts are provided; bit-for-bit identical
Apple-signed output is not a condition of rebuilding or exercising GPL rights.

## Preparing future releases

The capture command requires Python 3.11+, Xcode command-line tools, `curl`,
the installed Homebrew kegs and their SBOMs, and Homebrew patch metadata.
Source archives stay in an external/ignored cache; notices and the lock manifest
are committed under `third-party/macos-arm64/`.

```sh
python3 tools/compliance/macos.py capture --app /path/to/staged/Orbit.app \
  --cache build/compliance-sources --homebrew-metadata /path/to/homebrew-metadata.json
```

The metadata JSON has `revision` (the Homebrew/core commit) and `formulae`
(the matching formula metadata, including `local_patch_checksums`). The tool
matches staged library build UUIDs to installed kegs, verifies all downloaded
sources/patches against their recorded SHA-256, and preserves notices and recipes.
For an unchanged dependency inventory the existing manifest and a verified cache
can be reused. All cache files are checked again when packaging.
The captured `third-party/macos-arm64/homebrew-patches.json` is the default
metadata input for recapturing the same patch set. Update it against Homebrew's
matching package metadata when a formula's external file patches change.

Commit the release changes and tag the exact commit before packaging:

```sh
tools/filedrag/package-macos.sh build dist "<codesign identity>" build/compliance-sources
```

The packager refuses changed/unrecorded third-party binaries, altered legal
files, missing/corrupt source archives, a dirty checkout or an untagged commit.
It writes both the signed `Orbit.zip` and the complete source archive. Upload
both as assets on the same release, along with `SHA256SUMS`. Keep corresponding
source available alongside every binary you continue to distribute, including
older releases. No separate written-source-offer promise is used by this process.
