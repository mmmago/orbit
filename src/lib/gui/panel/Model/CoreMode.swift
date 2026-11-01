// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

/// Whether this Mac shares its keyboard and mouse (server) or uses another's (client)
enum CoreMode: String, Codable, Sendable {
    case none
    case server
    case client
}
