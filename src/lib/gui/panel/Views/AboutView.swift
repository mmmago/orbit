// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import AppKit
import SwiftUI

/// Orbit's icon, name and build, and the Deskflow project it is made from
struct AboutView: View {
    let updater: ReleaseUpdater
    private static let deskflowURL = URL(string: "https://github.com/deskflow/deskflow")!
    private static let coffeeURL = URL(string: "https://buymeacoffee.com/mmmago")!

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            header
                .frame(maxWidth: .infinity)
                .padding(.bottom, 8)

            SectionTitle(title: "Updates")
            UpdateSettingsView(updater: updater)

            SectionTitle(title: "Acknowledgements")
                .padding(.top, 8)
            SectionCard {
                VStack(alignment: .leading, spacing: 6) {
                    HStack {
                        DeskflowLogo()
                        Spacer()
                        Link(destination: Self.deskflowURL) {
                            HStack(spacing: 2) {
                                Text("GitHub")
                                Image(systemName: "arrow.up.right")
                                    .imageScale(.small)
                            }
                        }
                        .font(.callout)
                        .help("Deskflow on GitHub")
                    }
                    Text("Orbit is made from Deskflow, the open-source app that shares a keyboard and mouse between computers, and is released under the same GPL 2.0 license.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                .padding(.vertical, 12)
            }

            coffee
                .frame(maxWidth: .infinity)
                .padding(.top, 8)
        }
    }

    /// The icon, the name and the version, centred at the top
    private var header: some View {
        VStack(spacing: 4) {
            Image(nsImage: NSApp.applicationIconImage)
                .resizable()
                .frame(width: 64, height: 64)
                .accessibilityHidden(true)
            Text("Orbit")
                .font(.title2)
                .bold()
            Text(Self.versionText)
                .font(.callout)
                .foregroundStyle(.secondary)
                .monospacedDigit()
                .textSelection(.enabled)
        }
    }

    private var coffee: some View {
        Link(destination: Self.coffeeURL) {
            if let button = Bundle.main.image(forResource: "BuyMeACoffee") {
                Image(nsImage: button)
                    .resizable()
                    .scaledToFit()
                    .frame(width: 98)
            } else {
                Label("Buy me a coffee", systemImage: "cup.and.saucer.fill")
            }
        }
        .accessibilityLabel("Buy me a coffee")
        .help("Support Orbit on Buy Me a Coffee")
    }

    /// "Version 1.27.0 (build 36)": the last part of Orbit's version counts
    /// the commits since the release it is built from
    private static var versionText: String {
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
        let parts = version.split(separator: ".")
        guard !version.isEmpty else { return String(localized: "Development build") }
        guard parts.count == 4 else { return String(localized: "Version \(version)") }
        let release = parts.prefix(3).joined(separator: ".")
        return String(localized: "Version \(release) (build \(String(parts[3])))")
    }
}

/// Deskflow's logo: its coloured waves, and its name drawn in the text
/// colour so it reads in light and dark mode
private struct DeskflowLogo: View {
    private let mark = Bundle.main.image(forResource: "DeskflowMark")
    private let wordmark = Bundle.main.image(forResource: "DeskflowWordmark")

    var body: some View {
        HStack(spacing: 6) {
            if let mark {
                Image(nsImage: mark)
            }
            if let wordmark {
                Image(nsImage: wordmark)
                    .renderingMode(.template)
                    .foregroundStyle(.primary)
            } else {
                Text(verbatim: "Deskflow")
                    .font(.title2)
            }
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(Text(verbatim: "Deskflow"))
    }
}
