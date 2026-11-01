// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import SwiftUI

/// Client mode: the Orbit servers found on the network; clicking one
/// connects to it
struct ServerPicker: View {
    let model: PanelModel

    var body: some View {
        if model.state.servers.isEmpty {
            HStack(spacing: 8) {
                ProgressView()
                    .controlSize(.small)
                Text("Looking for Orbit servers on the network…")
                    .foregroundStyle(.secondary)
            }
            .padding(.vertical, 10)
            .accessibilityElement(children: .combine)
        } else {
            ForEach(Array(model.state.servers.enumerated()), id: \.element.id) { index, server in
                if index > 0 {
                    Divider()
                }
                ServerRow(
                    server: server,
                    isSelected: server.address == model.state.selectedServer,
                    phase: model.state.phase
                ) {
                    model.pickServer(server.address)
                }
            }
        }
    }
}

/// A found server: its name and address, and a check mark on the one this
/// Mac connects to
private struct ServerRow: View {
    let server: FoundServer
    let isSelected: Bool
    let phase: ConnectionPhase
    let action: () -> Void

    @State private var isHovered = false

    var body: some View {
        Button(action: action) {
            HStack(spacing: 10) {
                Image(systemName: "desktopcomputer")
                    .font(.title3)
                    .foregroundStyle(isSelected ? AnyShapeStyle(.green) : AnyShapeStyle(.secondary))
                    .frame(width: 24)
                    .accessibilityHidden(true)
                VStack(alignment: .leading, spacing: 2) {
                    Text(server.name)
                    if !server.detail.isEmpty {
                        Text(server.detail)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                            .truncationMode(.middle)
                    }
                }
                Spacer(minLength: 8)
                if isSelected {
                    if phase == .connecting {
                        ProgressView()
                            .controlSize(.small)
                    } else {
                        Image(systemName: "checkmark.circle.fill")
                            .foregroundStyle(.white, .green)
                            .accessibilityHidden(true)
                    }
                }
            }
            .padding(.vertical, 8)
            .contentShape(.rect)
        }
        .buttonStyle(.plain)
        .background {
            if isHovered && !isSelected {
                RoundedRectangle(cornerRadius: 6)
                    .fill(.fill.tertiary)
                    .padding(.horizontal, -6)
            }
        }
        .onHover { isHovered = $0 }
        .help(isSelected ? String(localized: "Connected to this Mac") : String(localized: "Connect to this Mac"))
        .accessibilityAddTraits(isSelected ? .isSelected : [])
    }
}
