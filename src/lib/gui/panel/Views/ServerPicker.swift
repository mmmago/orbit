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

/// Server mode: the clients connected to this Mac, with a check mark, then
/// the other Orbit computers found on the network: clients to connect, and
/// servers to set as client
struct ServerComputerList: View {
    let model: PanelModel

    var body: some View {
        let clients = model.state.arrangement.connectedClients
        if clients.isEmpty && model.state.servers.isEmpty {
            HStack(spacing: 8) {
                ProgressView()
                    .controlSize(.small)
                Text("Looking for Orbit computers on the network…")
                    .foregroundStyle(.secondary)
            }
            .padding(.vertical, 10)
            .accessibilityElement(children: .combine)
        } else {
            ForEach(Array(clients.enumerated()), id: \.element.id) { index, client in
                if index > 0 {
                    Divider()
                }
                ComputerLabel(
                    name: client.name,
                    detail: String(localized: "Uses the keyboard and mouse of this Mac"),
                    isSelected: true
                )
                .accessibilityElement(children: .combine)
            }
            ForEach(Array(model.state.servers.enumerated()), id: \.element.id) { index, server in
                if index > 0 || !clients.isEmpty {
                    Divider()
                }
                OtherServerRow(model: model, server: server)
            }
        }
    }
}

/// Server mode: another Orbit computer found on the network. One button
/// asks it to use the keyboard and mouse of this Mac: "Connect" for a
/// client, "Set as client" for a server
private struct OtherServerRow: View {
    let model: PanelModel
    let server: FoundServer

    private var request: SetAsClientRequest? {
        model.state.setAsClient.flatMap { $0.address == server.address ? $0 : nil }
    }

    var body: some View {
        HStack(spacing: 8) {
            ComputerLabel(name: server.name, detail: server.detail, error: request?.error)
                .accessibilityElement(children: .combine)
            if request?.isWaiting == true || server.isChanging {
                ProgressView()
                    .controlSize(.small)
                    .help(String(localized: "Waiting for \(server.name)…"))
                    .accessibilityLabel(Text("Waiting for \(server.name)"))
            } else if server.canSetAsClient {
                Button(server.isClient ? "Connect" : "Set as client") {
                    model.setAsClient(server.address)
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.small)
                .disabled(model.state.setAsClient?.isWaiting == true)
                .help(
                    server.isClient
                        ? String(localized: "\(server.name) connects to this Mac and uses its keyboard and mouse")
                        : String(localized: "\(server.name) uses the keyboard and mouse of this Mac, which becomes the server")
                )
            }
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
    /// Connects to it
    let connect: () -> Void

    @State private var isHovered = false

    private var request: SetAsClientRequest? {
        model.state.setAsClient.flatMap { $0.address == server.address ? $0 : nil }
    }

    var body: some View {
        HStack(spacing: 8) {
            Button(action: connect) {
                ComputerLabel(
                    name: server.name,
                    detail: server.detail,
                    error: request?.error,
                    isSelected: isSelected,
                    isConnecting: isSelected && phase == .connecting
                )
            }
            .buttonStyle(.plain)
            .onHover { isHovered = $0 }
            .help(isSelected ? String(localized: "Connected to this Mac") : String(localized: "Connect to this Mac"))
            .accessibilityAddTraits(isSelected ? .isSelected : [])
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
}

/// A computer's icon, name and detail, green with a check mark when
/// connected (a spinner while connecting)
private struct ComputerLabel: View {
    let name: String
    let detail: String
    var error: String?
    var isSelected = false
    var isConnecting = false

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: "desktopcomputer")
                .font(.title3)
                .foregroundStyle(isSelected ? AnyShapeStyle(.green) : AnyShapeStyle(.secondary))
                .frame(width: 24)
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: 2) {
                Text(name)
                if !detail.isEmpty {
                    Text(detail)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                }
                if let error {
                    Text(error)
                        .font(.caption)
                        .foregroundStyle(.red)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            Spacer(minLength: 8)
            if isConnecting {
                ProgressView()
                    .controlSize(.small)
            } else if isSelected {
                Image(systemName: "checkmark.circle.fill")
                    .foregroundStyle(.white, .green)
                    .accessibilityHidden(true)
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
