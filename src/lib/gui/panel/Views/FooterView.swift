// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

import SwiftUI

/// Connection status, restart and quit
struct FooterView: View {
    let model: PanelModel

    @State private var isConfirmingQuit = false

    var body: some View {
        HStack(spacing: 8) {
            StatusLogo(phase: model.state.phase)
            Text(model.state.statusText)
                .font(.callout)
                .lineLimit(1)
                .truncationMode(.tail)
            Spacer(minLength: 8)
            Button("Restart", systemImage: "arrow.clockwise", action: model.restart)
                .labelStyle(.iconOnly)
                .help("Restart")
            Button("Quit", role: .destructive, action: confirmQuit)
                .buttonStyle(.borderedProminent)
                .tint(.red)
                .help("Quit Orbit")
        }
        .alert("Quit Orbit?", isPresented: $isConfirmingQuit) {
            Button("Quit", role: .destructive, action: model.quit)
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Keyboard, mouse and file sharing stop until you open Orbit again.")
        }
    }

    private func confirmQuit() {
        isConfirmingQuit = true
    }
}
