// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

/// Options that apply when this Mac is the server
struct ServerOptions: Codable, Equatable, Sendable {
    var trackpadGestures = true
    var fileDragDrop = true
    var clipboard = true
}
