// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

/// A position in the server's grid of computers
struct GridCell: Codable, Hashable, Sendable {
    var column: Int
    var row: Int
}
