# Publishing Orbit updates

Orbit checks the latest stable, public release at
<https://github.com/mmmago/orbit/releases> on launch (if the last check was at
least six hours ago), then checks every six hours while running. Users can
disable automatic checks/downloads or check manually in **About Orbit**.
Drafts and prereleases are ignored. A repository with no public releases
shows “No public Orbit releases are available yet.”

## Prepare a release

1. Tag the release `vMAJOR.MINOR.PATCH` (for example `v1.27.1`). Orbit also
   accepts a fourth numeric build component. Versions compare numerically;
   use a version higher than the installed build. The inherited development
   build `1.27.0.37`, for example, updates to `1.27.1`.
2. Build from that tag. Confirm `Orbit.app/Contents/Info.plist` has the same
   `CFBundleShortVersionString` as the tag (a trailing `.0` is equivalent).
   CMake derives the version from `v*` tags; reconfigure after tagging.
3. Package a self-contained, signed `Orbit.app` using
   `tools/filedrag/package-macos.sh`. Keep the same signing identity/team
   and bundle identifier (`io.github.mmmago.orbit`) across releases.
   For public distribution, use Developer ID signing and notarization.
4. Upload a ZIP containing `Orbit.app` at its root as a **release asset**:
   - `Orbit-macos-arm64.zip` for Apple Silicon;
   - `Orbit-macos-x86_64.zip` for Intel;
   - `Orbit-macos-universal.zip` for a universal build.
   The existing packager's `Orbit.zip` is also accepted as a fallback. It
   must support the user's architecture. GitHub-generated source ZIPs are
   never used as updates.
5. Publish the release as the latest stable release. No appcast, token, or
   separate update server is needed. Metadata follows GitHub's
   [latest-release API](https://docs.github.com/en/rest/releases/releases#get-the-latest-release).

## Installation behavior

Orbit downloads the matching ZIP, checks its size and GitHub SHA-256 digest
when supplied, then verifies the app's version, bundle identity, architecture,
minimum macOS version when declared, and code signature against the installed
app's designated requirement. Unsigned or differently signed updates require
manual installation.

After verification, a native dialog offers **Install and Restart** or
**Later**. The About page also offers installation of the staged update.
Finish file transfers before installing: Orbit quits normally to stop its
core, replaces the app, then reopens it. The previous app is restored if
replacement or reopening fails. Settings and certificates are retained.
Installation requires a writable app and parent directory; apps on a disk
image or in App Translocation must first be moved to Applications.

The ready download is kept for the running session. After quitting without
installing, a later check may download it again. No computer name, local
account name, or language is sent; the API request identifies only Orbit
and its version.

## Validation

Build with `BUILD_TESTS=ON`, then run:

```sh
cmake --build build --target ReleaseUpdateTests
ctest --test-dir build/src/unittests -R 'ReleaseUpdateTests|InstallUpdateTests' --output-on-failure
```

The tests cover numeric version ordering, release asset selection, permitted
download URLs, ZIP size/checksum checks, release version matching, and code
signature tampering, successful replacement, rollback, and quit timeout.
They use temporary ad-hoc signed fixtures and do not
replace or launch an installed Orbit app.
