// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

import CryptoKit
import Foundation

/// Standalone Swift tests, registered with CTest alongside the Qt tests.
@main
struct ReleaseUpdateTests {
    static func main() async throws {
        try versions()
        try releaseAssets()
        try await archiveValidation()
        print("ReleaseUpdateTests passed: versions, asset selection, archive integrity, bundle identity, signatures.")
    }

    private static func expect(_ condition: @autoclosure () -> Bool, _ message: String) throws {
        if !condition() { throw UpdateError.message(message) }
    }

    private static func versions() throws {
        try expect(ReleaseVersion("v1.27.0") == ReleaseVersion("1.27.0.0"), "Tags and zero build numbers must match")
        let ordered = ["1.9.9", "1.10.0", "1.27.0", "1.27.0.37", "1.27.0.100", "1.27.1", "2.0.0"]
        for (left, right) in zip(ordered, ordered.dropFirst()) {
            guard let lhs = ReleaseVersion(left), let rhs = ReleaseVersion(right) else {
                throw UpdateError.message("Valid versions were rejected")
            }
            try expect(lhs < rhs, "Incorrect version order: \(left), \(right)")
        }
        for invalid in ["", "v", "1", "1.2", "1..2", "1.2.3.4.5", "1.2.3-rc1", "1.2.3\n", "1.2.-3", "1.2.３", "latest"] {
            try expect(ReleaseVersion(invalid) == nil, "Invalid tag accepted: \(invalid)")
        }
    }

    private static func releaseAssets() throws {
        let json = #"{"tag_name":"v1.28.0","draft":false,"prerelease":false,"assets":[{"name":"Orbit-macos-arm64.zip","size":42,"digest":null,"browser_download_url":"https://github.com/mmmago/orbit/releases/download/v1.28.0/arm.zip"},{"name":"Orbit-macos-x86_64.zip","size":42,"browser_download_url":"https://github.com/mmmago/orbit/releases/download/v1.28.0/intel.zip"},{"name":"Orbit-macos-universal.zip","size":42,"browser_download_url":"https://github.com/mmmago/orbit/releases/download/v1.28.0/universal.zip"}]}"#
        let release = try JSONDecoder().decode(GitHubRelease.self, from: Data(json.utf8))
        try expect(release.asset(for: "arm64")?.name == "Orbit-macos-arm64.zip", "Wrong Apple Silicon asset")
        try expect(release.asset(for: "x86_64")?.name == "Orbit-macos-x86_64.zip", "Wrong Intel asset")
        try expect(release.asset(for: "other")?.name == "Orbit-macos-universal.zip", "Universal fallback failed")
        let legacy = ReleaseAsset(name: "Orbit.zip", browserDownloadURL: URL(string: "https://github.com/mmmago/orbit/releases/download/v1.28.0/Orbit.zip")!, size: 42, digest: nil)
        let fallback = GitHubRelease(tagName: "v1.28.0", draft: false, prerelease: false, assets: [legacy])
        try expect(fallback.asset(for: "arm64")?.name == "Orbit.zip", "Existing package format must work")
        for address in ["http://github.com/mmmago/orbit/releases/download/v1/x.zip", "https://evil.example/x.zip", "https://github.com/other/orbit/releases/download/v1/x.zip", "https://github.com/mmmago/orbit/archive/v1.zip"] {
            let bad = ReleaseAsset(name: "Orbit.zip", browserDownloadURL: URL(string: address)!, size: 42, digest: nil)
            let unsafe = GitHubRelease(tagName: "v1.28.0", draft: false, prerelease: false, assets: [bad])
            try expect(unsafe.asset(for: "arm64") == nil, "Untrusted asset was accepted")
        }
    }

    private static func archiveValidation() async throws {
        let files = FileManager.default
        let directory = files.temporaryDirectory.appending(path: "Orbit-update-tests-\(UUID().uuidString)")
        try files.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? files.removeItem(at: directory) }
        let current = directory.appending(path: "Current.app")
        let updated = directory.appending(path: "Orbit.app")
        try makeApp(at: current, version: "1.27.0")
        try makeApp(at: updated, version: "1.28.0")
        let archive = directory.appending(path: "Orbit.zip")
        try run("/usr/bin/ditto", ["-c", "-k", "--keepParent", updated.path, archive.path])
        let bytes = try Data(contentsOf: archive)
        let digest = SHA256.hash(data: bytes).map { ($0 < 16 ? "0" : "") + String($0, radix: 16) }.joined()
        let asset = ReleaseAsset(name: "Orbit.zip", browserDownloadURL: URL(string: "https://github.com/mmmago/orbit/releases/download/v1.28.0/Orbit.zip")!, size: Int64(bytes.count), digest: "sha256:\(digest)")
        let installer = UpdateInstaller()
        guard let version = ReleaseVersion("1.28.0"), let wrongVersion = ReleaseVersion("1.29.0") else {
            throw UpdateError.message("Test versions failed to parse")
        }
        let staged = try await installer.stage(archive: archive, asset: asset, version: version, currentApp: current)
        await installer.discard(app: staged)
        try await rejected("Mismatched release version") {
            try await installer.stage(archive: archive, asset: asset, version: wrongVersion, currentApp: current)
        }
        let wrongSize = ReleaseAsset(name: asset.name, browserDownloadURL: asset.browserDownloadURL, size: asset.size + 1, digest: nil)
        try await rejected("Truncated archive") {
            try await installer.stage(archive: archive, asset: wrongSize, version: version, currentApp: current)
        }
        let wrongDigest = ReleaseAsset(name: asset.name, browserDownloadURL: asset.browserDownloadURL, size: asset.size, digest: "sha256:" + String(repeating: "0", count: 64))
        try await rejected("Checksum mismatch") {
            try await installer.stage(archive: archive, asset: wrongDigest, version: version, currentApp: current)
        }
        // Repack a tampered, previously signed app. Omitting the optional digest
        // must still fail signature validation.
        try Data("tampered".utf8).write(to: updated.appending(path: "Contents/MacOS/Orbit"))
        try files.removeItem(at: archive)
        try run("/usr/bin/ditto", ["-c", "-k", "--keepParent", updated.path, archive.path])
        let tamperedSize = (try files.attributesOfItem(atPath: archive.path)[.size] as? NSNumber)?.int64Value ?? 0
        let tampered = ReleaseAsset(name: asset.name, browserDownloadURL: asset.browserDownloadURL, size: tamperedSize, digest: nil)
        try await rejected("Tampered signature") {
            try await installer.stage(archive: archive, asset: tampered, version: version, currentApp: current)
        }
    }

    private static func rejected(_ name: String, operation: () async throws -> URL) async throws {
        do {
            let unexpected = try await operation()
            try? FileManager.default.removeItem(at: unexpected.deletingLastPathComponent())
        } catch { return }
        throw UpdateError.message("\(name) was accepted")
    }

    private static func makeApp(at app: URL, version: String) throws {
        let executable = app.appending(path: "Contents/MacOS/Orbit")
        try FileManager.default.createDirectory(at: executable.deletingLastPathComponent(), withIntermediateDirectories: true)
        try FileManager.default.copyItem(at: URL(filePath: CommandLine.arguments[0]), to: executable)
        let info: [String: String] = ["CFBundleIdentifier": "io.github.mmmago.orbit", "CFBundleExecutable": "Orbit",
                                      "CFBundlePackageType": "APPL", "CFBundleShortVersionString": version]
        let data = try PropertyListSerialization.data(fromPropertyList: info, format: .xml, options: 0)
        try data.write(to: app.appending(path: "Contents/Info.plist"))
        // A test-only designated requirement lets two ad-hoc signed fixtures
        // represent releases from the same developer without a private key.
        try run("/usr/bin/codesign", ["--force", "--sign", "-", "--identifier", "io.github.mmmago.orbit",
                                    "--requirements", "=designated => identifier \"io.github.mmmago.orbit\"", app.path])
    }

    private static func run(_ executable: String, _ arguments: [String]) throws {
        let process = Process()
        process.executableURL = URL(filePath: executable)
        process.arguments = arguments
        process.standardError = FileHandle.nullDevice
        try process.run()
        process.waitUntilExit()
        try expect(process.terminationStatus == 0, "Fixture command failed: \(executable)")
    }
}
