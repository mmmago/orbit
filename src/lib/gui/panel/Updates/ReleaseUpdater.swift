// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import AppKit
import Observation

/// Checks on launch and every six hours. Downloads stay staged until the user
/// agrees to quit, replace Orbit, and relaunch through its normal quit action.
@MainActor
@Observable
final class ReleaseUpdater {
    private(set) var status = String(localized: "Updates come from mmmago/orbit on GitHub.")
    private(set) var isBusy = false
    private(set) var readyVersion: String?

    var automaticallyChecks: Bool {
        didSet { defaults.set(automaticallyChecks, forKey: Self.automaticKey) }
    }

    @ObservationIgnored var quit: () -> Void = {}
    @ObservationIgnored private let defaults: UserDefaults
    @ObservationIgnored private let installer = UpdateInstaller()
    @ObservationIgnored private var readyApp: URL?
    @ObservationIgnored private var readyNumber: ReleaseVersion?
    @ObservationIgnored private var task: Task<Void, Never>?
    @ObservationIgnored private var schedule: Task<Void, Never>?

    private static let automaticKey = "OrbitAutomaticallyChecksForUpdates"
    private static let lastCheckKey = "OrbitLastReleaseCheck"
    private static let interval: TimeInterval = 6 * 60 * 60
    static let releasesURL = URL(string: "https://github.com/mmmago/orbit/releases")!

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        automaticallyChecks = defaults.object(forKey: Self.automaticKey) as? Bool ?? true
    }

    func start() {
        guard schedule == nil else { return }
        schedule = Task { [weak self] in
            while !Task.isCancelled {
                self?.checkAutomatically()
                do { try await Task.sleep(for: .seconds(60 * 60)) } catch { return }
            }
        }
    }

    func checkNow() {
        beginCheck(manual: true)
    }

    func cancel() {
        task?.cancel()
    }

    func install() {
        guard !isBusy, let readyApp, let readyNumber else { return }
        isBusy = true
        status = String(localized: "Preparing to restart…")
        task = Task {
            defer { isBusy = false }
            do {
                try await installer.launchReplacement(app: readyApp, version: readyNumber,
                                                      currentApp: Bundle.main.bundleURL,
                                                      processID: ProcessInfo.processInfo.processIdentifier)
                quit()
            } catch {
                status = error.localizedDescription
                showError(error.localizedDescription)
            }
        }
    }

    private func checkAutomatically() {
        let last = defaults.object(forKey: Self.lastCheckKey) as? Date ?? .distantPast
        guard automaticallyChecks, Date.now.timeIntervalSince(last) >= Self.interval else { return }
        beginCheck(manual: false)
    }

    private func beginCheck(manual: Bool) {
        guard !isBusy else { return }
        isBusy = true
        task = Task {
            defer { isBusy = false }
            do {
                try await check(manual: manual)
            } catch {
                if Task.isCancelled {
                    status = String(localized: "Update download cancelled.")
                } else {
                    status = error.localizedDescription
                    if manual { showError(error.localizedDescription) }
                }
            }
        }
    }

    private func check(manual: Bool) async throws {
        guard let installed = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String,
              let current = ReleaseVersion(installed) else {
            throw UpdateError.message("Run a packaged copy of Orbit to check for updates.")
        }
        status = String(localized: "Checking for updates…")
        var request = URLRequest(url: URL(string: "https://api.github.com/repos/mmmago/orbit/releases/latest")!)
        request.timeoutInterval = 30
        request.cachePolicy = .reloadIgnoringLocalCacheData
        request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        request.setValue("2022-11-28", forHTTPHeaderField: "X-GitHub-Api-Version")
        request.setValue("Orbit/\(installed)", forHTTPHeaderField: "User-Agent")
        let (data, response) = try await URLSession.shared.data(for: request)
        let code = (response as? HTTPURLResponse)?.statusCode
        if code == 404 {
            defaults.set(Date.now, forKey: Self.lastCheckKey)
            status = String(localized: "No public Orbit releases are available yet.")
            return
        }
        guard code == 200 else {
            throw UpdateError.message("GitHub could not be reached or its request limit was reached. Please try again later.")
        }
        let release = try JSONDecoder().decode(GitHubRelease.self, from: data)
        guard !release.draft, !release.prerelease, let latest = ReleaseVersion(release.tagName) else {
            throw UpdateError.message("GitHub's latest release does not have a supported stable version tag.")
        }
        defaults.set(Date.now, forKey: Self.lastCheckKey)
        guard latest > current else {
            status = String(localized: "Orbit is up to date.")
            return
        }
        if readyNumber == latest {
            status = String(localized: "\(release.tagName) is ready to install.")
            if manual { promptToInstall(version: release.tagName) }
            return
        }
        guard let asset = release.asset(for: GitHubRelease.architecture) else {
            throw UpdateError.message("A newer Orbit release is available, but it has no compatible Mac ZIP. Visit GitHub Releases to install it manually.")
        }
        status = String(localized: "Downloading \(release.tagName)…")
        var download = URLRequest(url: asset.browserDownloadURL)
        download.timeoutInterval = 30 * 60
        let (archive, downloadResponse) = try await URLSession.shared.download(for: download)
        defer { try? FileManager.default.removeItem(at: archive) }
        guard (downloadResponse as? HTTPURLResponse)?.statusCode == 200,
              downloadResponse.url?.scheme == "https" else {
            throw UpdateError.message("The update could not be downloaded. Please try again.")
        }
        try Task.checkCancellation()
        status = String(localized: "Verifying \(release.tagName)…")
        let app = try await installer.stage(archive: archive, asset: asset, version: latest, currentApp: Bundle.main.bundleURL)
        if Task.isCancelled {
            await installer.discard(app: app)
            throw CancellationError()
        }
        if let old = readyApp { await installer.discard(app: old) }
        readyApp = app
        readyNumber = latest
        readyVersion = release.tagName
        status = String(localized: "\(release.tagName) is ready to install.")
        // Clear busy before the alert, so its Install button can start installation.
        isBusy = false
        promptToInstall(version: release.tagName)
    }

    private func promptToInstall(version: String) {
        let alert = NSAlert()
        alert.messageText = String(localized: "Install Orbit \(version)?")
        alert.informativeText = String(localized: "The update has been downloaded and verified. Orbit will restart to install it. Keyboard, mouse and file sharing will pause; finish any transfers first.")
        alert.addButton(withTitle: String(localized: "Install and Restart"))
        alert.addButton(withTitle: String(localized: "Later"))
        NSApp.activate()
        if alert.runModal() == .alertFirstButtonReturn {
            // Run after the check's defer clears its busy state.
            Task { install() }
        }
    }

    private func showError(_ message: String) {
        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = String(localized: "Orbit could not update")
        alert.informativeText = message
        alert.addButton(withTitle: String(localized: "OK"))
        alert.addButton(withTitle: String(localized: "GitHub Releases"))
        NSApp.activate()
        if alert.runModal() == .alertSecondButtonReturn {
            NSWorkspace.shared.open(Self.releasesURL)
        }
    }
}
