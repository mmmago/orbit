// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

/// An Orbit server found on the network with Bonjour
struct FoundServer: Codable, Identifiable, Equatable, Sendable {
    var name: String
    var address: String
    var detail: String
    /// Its Orbit takes "Set as client" from this Mac
    var canSetAsClient: Bool

    var id: String { address }
}
