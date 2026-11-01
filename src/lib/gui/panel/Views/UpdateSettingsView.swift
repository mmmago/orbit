// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import SwiftUI

/// The updates group: the automatic download switch, then where the updater
/// is and its buttons on one row
struct UpdateSettingsView: View {
    @Bindable var updater: ReleaseUpdater

    var body: some View {
        SectionCard {
            OptionToggle(
                title: "Automatically download updates",
                isOn: $updater.automaticallyChecks
            )
            Divider()
            HStack(spacing: 8) {
                if updater.isBusy {
                    ProgressView()
                        .controlSize(.small)
                        .accessibilityLabel("Updating Orbit")
                }
                Text(updater.status)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                Spacer(minLength: 8)
                buttons
                    .controlSize(.small)
                    .fixedSize()
            }
            .padding(.vertical, 8)
        }
    }

    @ViewBuilder private var buttons: some View {
        if updater.readyVersion != nil {
            Button("Install and Restart", action: updater.install)
                .buttonStyle(.borderedProminent)
                .disabled(updater.isBusy)
        } else if updater.isBusy {
            Button("Cancel", action: updater.cancel)
        } else {
            Button("Check for Updates…", action: updater.checkNow)
        }
    }
}
