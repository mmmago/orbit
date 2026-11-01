// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

import SwiftUI

struct UpdateSettingsView: View {
    @Bindable var updater: ReleaseUpdater

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Toggle("Automatically download updates", isOn: $updater.automaticallyChecks)
                .font(.callout)
            HStack {
                if updater.isBusy {
                    ProgressView()
                        .controlSize(.small)
                        .accessibilityLabel("Updating Orbit")
                }
                Text(updater.status)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                Spacer(minLength: 0)
            }
            HStack {
                Button("Check for Updates…", action: updater.checkNow)
                    .disabled(updater.isBusy)
                if updater.readyVersion != nil {
                    Button("Install and Restart", action: updater.install)
                        .disabled(updater.isBusy)
                }
                if updater.isBusy, updater.readyVersion == nil {
                    Button("Cancel", action: updater.cancel)
                }
            }
            .controlSize(.small)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}
