// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import SwiftUI

/// Client mode: the Orbit servers found on the network; clicking one
/// connects to it, "Set as client" makes it a client of this Mac instead
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
                let isSelected = server.address == model.state.selectedServer
                FoundServerRow(
                    model: model,
                    server: server,
                    isSelected: isSelected,
                    phase: model.state.phase,
                    // no need while this Mac uses its keyboard and mouse
                    offersSetAsClient: !(isSelected && model.state.phase == .connected)
                ) {
                    model.pickServer(server.address)
                }
            }
        }
    }
}

/// Server mode: the other Orbit servers found on the network, each of which
/// can be set as a client of this Mac
struct OtherServerList: View {
    let model: PanelModel

    var body: some View {
        ForEach(Array(model.state.servers.enumerated()), id: \.element.id) { index, server in
            if index > 0 {
                Divider()
            }
            FoundServerRow(model: model, server: server)
        }
    }
}

/// A found server: its name and address, a check mark on the one this Mac
/// connects to, and "Set as client"
private struct FoundServerRow: View {
    let model: PanelModel
    let server: FoundServer
    var isSelected = false
    var phase = ConnectionPhase.idle
    var offersSetAsClient = true
    /// Connects to it; nil where it isn't a server to connect to
    var connect: (() -> Void)?

    @State private var isHovered = false

    private var request: SetAsClientRequest? {
        model.state.setAsClient.flatMap { $0.address == server.address ? $0 : nil }
    }

    var body: some View {
        HStack(spacing: 8) {
            if let connect {
                Button(action: connect) { label }
                    .buttonStyle(.plain)
                    .onHover { isHovered = $0 }
                    .help(isSelected ? String(localized: "Connected to this Mac") : String(localized: "Connect to this Mac"))
                    .accessibilityAddTraits(isSelected ? .isSelected : [])
            } else {
                label
                    .accessibilityElement(children: .combine)
            }
            if offersSetAsClient && server.canSetAsClient {
                SetAsClientButton(
                    serverName: server.name,
                    isWaiting: request?.isWaiting == true,
                    isDisabled: model.state.setAsClient?.isWaiting == true
                ) {
                    model.setAsClient(server.address)
                }
            }
        }
        .background {
            if isHovered && !isSelected {
                RoundedRectangle(cornerRadius: 6)
                    .fill(.fill.tertiary)
                    .padding(.horizontal, -6)
            }
        }
    }

    private var label: some View {
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
                if let error = request?.error {
                    Text(error)
                        .font(.caption)
                        .foregroundStyle(.red)
                        .fixedSize(horizontal: false, vertical: true)
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
}

/// Asks the server's Orbit to become a client of this Mac; a spinner while
/// it answers
private struct SetAsClientButton: View {
    let serverName: String
    let isWaiting: Bool
    let isDisabled: Bool
    let action: () -> Void

    var body: some View {
        if isWaiting {
            ProgressView()
                .controlSize(.small)
                .help(String(localized: "Waiting for \(serverName)…"))
                .accessibilityLabel(Text("Waiting for \(serverName)"))
        } else {
            Button("Set as client", action: action)
                .controlSize(.small)
                .disabled(isDisabled)
                .help(String(localized: "\(serverName) uses the keyboard and mouse of this Mac, which becomes the server"))
        }
    }
}
