// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

/// Everything the panel shows, sent by Orbit as JSON whenever it changes
struct PanelState: Codable, Equatable, Sendable {
    var mode = CoreMode.none
    var phase = ConnectionPhase.idle
    var statusText = ""
    /// The menu bar icon follows the connection state, like the status logo
    var animatedMenuBarIcon = false
    var server = ServerOptions()
    var client = ClientOptions()
    var servers: [FoundServer] = []
    /// Address of the found server matching `client.host`, if any
    var selectedServer: String?
    /// The found server asked to become a client of this Mac, while waiting
    /// for its answer or when it failed
    var setAsClient: SetAsClientRequest?
    var arrangement = Arrangement()
}
