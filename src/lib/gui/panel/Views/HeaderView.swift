// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import AppKit
import SwiftUI

/// App icon, name and what Orbit does
struct HeaderView: View {
    var body: some View {
        HStack(spacing: 12) {
            Image(nsImage: NSApp.applicationIconImage)
                .resizable()
                .frame(width: 44, height: 44)
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: 2) {
                Text("Orbit")
                    .font(.title2)
                    .bold()
                Text("Keyboard, mouse, trackpad and file sharing between Macs")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
            // room for the About button in the corner
            Spacer(minLength: 20)
        }
    }
}
