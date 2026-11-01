// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

import SwiftUI

/// The server and client cards, side by side
struct ModeSelector: View {
    let selection: CoreMode
    let onSelect: (CoreMode) -> Void

    var body: some View {
        HStack(alignment: .top, spacing: 10) {
            ModeCard(
                mode: .server,
                title: "Use this Mac's keyboard and mouse",
                detail: "Server",
                isSelected: selection == .server,
                action: selectServer
            )
            ModeCard(
                mode: .client,
                title: "Use another Mac's keyboard and mouse",
                detail: "Client",
                isSelected: selection == .client,
                action: selectClient
            )
        }
        .fixedSize(horizontal: false, vertical: true)
    }

    private func selectServer() {
        onSelect(.server)
    }

    private func selectClient() {
        onSelect(.client)
    }
}
