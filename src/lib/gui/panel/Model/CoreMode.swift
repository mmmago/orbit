// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

/// Whether this Mac shares its keyboard and mouse (server) or uses another's (client)
enum CoreMode: String, Codable, Sendable {
    case none
    case server
    case client
}
