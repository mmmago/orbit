// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

/// Options that apply when this Mac is a client
struct ClientOptions: Codable, Equatable, Sendable {
    var invertHorizontalScroll = false
    var invertVerticalScroll = false
    /// The server address this client connects to (host name or IP)
    var host = ""
}
