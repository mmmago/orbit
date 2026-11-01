// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

import SwiftUI

/// A computer drawn as a screen. The server's screen has a menu bar, like
/// the main display in the macOS display arrangement.
struct ScreenTile: View {
    let computer: ArrangedComputer
    let isLifted: Bool

    var body: some View {
        RoundedRectangle(cornerRadius: 6)
            .fill(wallpaper)
            .overlay(alignment: .top) {
                if computer.isServer {
                    Rectangle()
                        .fill(.white.opacity(0.85))
                        .frame(height: 7)
                }
            }
            .clipShape(.rect(cornerRadius: 6))
            .overlay {
                VStack(spacing: 1) {
                    Text(computer.name)
                        .font(.footnote)
                        .bold()
                        .lineLimit(1)
                        .truncationMode(.middle)
                    (computer.isServer ? Text("Server") : Text("Client"))
                        .font(.caption)
                        .opacity(0.8)
                }
                .foregroundStyle(.white)
                .padding(.horizontal, 6)
            }
            .overlay {
                RoundedRectangle(cornerRadius: 6)
                    .strokeBorder(.black.opacity(0.35), lineWidth: 1)
            }
            .padding(1)
            .shadow(color: .black.opacity(isLifted ? 0.35 : 0), radius: 6, y: 3)
    }

    private var wallpaper: LinearGradient {
        let colors: [Color] = computer.isServer
            ? [Color(red: 0.31, green: 0.55, blue: 0.84), Color(red: 0.12, green: 0.25, blue: 0.48)]
            : [Color(red: 0.43, green: 0.50, blue: 0.58), Color(red: 0.18, green: 0.22, blue: 0.28)]
        return LinearGradient(colors: colors, startPoint: .top, endPoint: .bottom)
    }
}
