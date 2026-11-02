// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

/// Options that apply when this Mac is the server
struct ServerOptions: Codable, Equatable, Sendable {
    var trackpadGestures = true
    var fileDragDrop = true
    var clipboard = true
    /// Touch ID to unlock a client shows in the menu bar icon, not a panel
    var touchIdInMenuBar = false
}
