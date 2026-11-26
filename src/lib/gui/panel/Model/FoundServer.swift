// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

/// An Orbit computer found on the network with Bonjour: a server, or (in
/// server mode only) a client not connected to this Mac
struct FoundServer: Codable, Identifiable, Equatable, Sendable {
    var name: String
    var address: String
    var detail: String
    /// Its Orbit takes "Set as client" (or "Connect") from this Mac
    var canSetAsClient: Bool
    /// In client mode: "Connect" asks it to connect to this Mac
    var isClient = false
    /// Switching modes (a client that left, or set as client): its button
    /// would be stale, a spinner shows instead
    var isChanging = false

    var id: String { address }
}
