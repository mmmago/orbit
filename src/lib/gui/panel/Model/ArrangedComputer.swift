// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

/// A computer placed in the server's grid
struct ArrangedComputer: Codable, Identifiable, Equatable, Sendable {
    var name: String
    var column: Int
    var row: Int
    var isServer: Bool
    var isConnected: Bool

    var id: String { name }
    var cell: GridCell { GridCell(column: column, row: row) }

    /// The arrangement shows the server and the clients connected to it
    var isShown: Bool { isServer || isConnected }
}
