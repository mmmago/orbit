// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import CryptoKit
import Foundation
import Security

/// Archive work runs away from the UI. The replacement is staged on the same
/// volume as Orbit so the helper can rename it after Orbit and its core quit.
actor UpdateInstaller {
    private let fileManager = FileManager.default

    func stage(archive: URL, asset: ReleaseAsset, version: ReleaseVersion, currentApp: URL) async throws -> URL {
        let attributes = try fileManager.attributesOfItem(atPath: archive.path)
        guard (attributes[.size] as? NSNumber)?.int64Value == asset.size else {
            throw UpdateError.message("The update download is incomplete. Please try again.")
        }
        if let digest = asset.digest {
            guard digest.hasPrefix("sha256:"), try checksum(archive) == String(digest.dropFirst(7)).lowercased() else {
                throw UpdateError.message("The update's checksum does not match GitHub. Please try again.")
            }
        }

        let directory = fileManager.temporaryDirectory.appending(path: "Orbit-update-\(UUID().uuidString)", directoryHint: .isDirectory)
        try fileManager.createDirectory(at: directory, withIntermediateDirectories: true)
        do {
            try await run("/usr/bin/ditto", ["-x", "-k", archive.path, directory.path])
            let app = directory.appending(path: "Orbit.app", directoryHint: .isDirectory)
            try validate(app: app, version: version, currentApp: currentApp)
            try await run("/usr/bin/lipo", ["-verify_arch", GitHubRelease.architecture, app.appending(path: "Contents/MacOS/Orbit").path])
            return app
        } catch {
            try? fileManager.removeItem(at: directory)
            throw error
        }
    }

    /// Called only after the user chooses Install and Restart.
    func launchReplacement(app: URL, version: ReleaseVersion, currentApp: URL, processID: Int32) async throws {
        let destination = currentApp.resolvingSymlinksInPath()
        let parent = destination.deletingLastPathComponent()
        guard destination.pathExtension == "app", !destination.path.contains("/AppTranslocation/"),
              fileManager.isWritableFile(atPath: parent.path), fileManager.isWritableFile(atPath: destination.path)
        else {
            throw UpdateError.message("Move Orbit to a writable Applications folder, then try again. You can also install the release from GitHub manually.")
        }

        let work = parent.appending(path: ".orbit-update-\(UUID().uuidString)", directoryHint: .isDirectory)
        try fileManager.createDirectory(at: work, withIntermediateDirectories: false)
        do {
            let replacement = work.appending(path: "Orbit.app", directoryHint: .isDirectory)
            try await run("/usr/bin/ditto", [app.path, replacement.path])
            try validate(app: replacement, version: version, currentApp: currentApp)
            let helper = work.appending(path: "install.sh")
            guard let resource = Bundle.main.url(forResource: "install-update", withExtension: "sh") else {
                throw UpdateError.message("The update installer is missing. Please reinstall Orbit from GitHub.")
            }
            try fileManager.copyItem(at: resource, to: helper)
            let process = Process()
            process.executableURL = URL(filePath: "/bin/bash")
            process.arguments = [helper.path, String(processID), destination.path, work.path]
            process.standardOutput = FileHandle.nullDevice
            process.standardError = FileHandle.nullDevice
            try process.run()
        } catch {
            try? fileManager.removeItem(at: work)
            throw error
        }
    }

    func discard(app: URL) {
        try? fileManager.removeItem(at: app.deletingLastPathComponent())
    }

    private func checksum(_ url: URL) throws -> String {
        let file = try FileHandle(forReadingFrom: url)
        defer { try? file.close() }
        var hash = SHA256()
        while let data = try file.read(upToCount: 1024 * 1024), !data.isEmpty {
            hash.update(data: data)
        }
        return hash.finalize().map { ($0 < 16 ? "0" : "") + String($0, radix: 16) }.joined()
    }

    private func validate(app: URL, version: ReleaseVersion, currentApp: URL) throws {
        let infoURL = app.appending(path: "Contents/Info.plist")
        let data = try Data(contentsOf: infoURL)
        let currentData = try Data(contentsOf: currentApp.appending(path: "Contents/Info.plist"))
        let currentInfo = try PropertyListSerialization.propertyList(from: currentData, format: nil) as? [String: Any]
        guard let info = try PropertyListSerialization.propertyList(from: data, format: nil) as? [String: Any],
              currentInfo?["CFBundleIdentifier"] as? String == "io.github.mmmago.orbit",
              info["CFBundleIdentifier"] as? String == "io.github.mmmago.orbit",
              info["CFBundleExecutable"] as? String == "Orbit",
              let number = info["CFBundleShortVersionString"] as? String,
              ReleaseVersion(number) == version,
              (try app.resourceValues(forKeys: [.isSymbolicLinkKey])).isSymbolicLink == false
        else {
            throw UpdateError.message("This archive does not contain the expected Orbit release.")
        }
        if let minimum = info["LSMinimumSystemVersion"] as? String {
            let parts = minimum.split(separator: ".").compactMap { Int($0) }
            guard !parts.isEmpty, ProcessInfo.processInfo.isOperatingSystemAtLeast(
                OperatingSystemVersion(majorVersion: parts[0], minorVersion: parts.count > 1 ? parts[1] : 0,
                                       patchVersion: parts.count > 2 ? parts[2] : 0)) else {
                throw UpdateError.message("This update requires macOS \(minimum) or later.")
            }
        }

        var installedCode: SecStaticCode?
        var newCode: SecStaticCode?
        var requirement: SecRequirement?
        let flags = SecCSFlags(rawValue: kSecCSStrictValidate | kSecCSCheckAllArchitectures | kSecCSCheckNestedCode)
        guard SecStaticCodeCreateWithPath(currentApp as CFURL, [], &installedCode) == errSecSuccess,
              let installedCode,
              SecCodeCopyDesignatedRequirement(installedCode, [], &requirement) == errSecSuccess,
              let requirement,
              SecStaticCodeCreateWithPath(app as CFURL, [], &newCode) == errSecSuccess,
              let newCode,
              SecStaticCodeCheckValidity(newCode, flags, requirement) == errSecSuccess else {
            throw UpdateError.message("The update's signature does not match this copy of Orbit. Install a release signed by the same developer, or update manually from GitHub.")
        }
    }

    private func run(_ executable: String, _ arguments: [String]) async throws {
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, any Error>) in
            let process = Process()
            process.executableURL = URL(filePath: executable)
            process.arguments = arguments
            process.standardOutput = FileHandle.nullDevice
            process.standardError = FileHandle.nullDevice
            process.terminationHandler = { process in
                if process.terminationStatus == 0 {
                    continuation.resume()
                } else {
                    continuation.resume(throwing: UpdateError.message("The update could not be unpacked or verified for this Mac. Please download a compatible Orbit release from GitHub."))
                }
            }
            do { try process.run() } catch { continuation.resume(throwing: error) }
        }
    }
}
