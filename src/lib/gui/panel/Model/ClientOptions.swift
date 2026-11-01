// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

/// Options that apply when this Mac is a client
struct ClientOptions: Codable, Equatable, Sendable {
    var invertHorizontalScroll = false
    var invertVerticalScroll = false
    /// The server address this client connects to (host name or IP)
    var host = ""
}
