// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-04 by Lucas Magotteaux; see NOTICE.md.

/// "Set as client" sent to a found server: waiting for its answer, or why it
/// failed
struct SetAsClientRequest: Codable, Equatable, Sendable {
    var address: String
    var error: String?

    var isWaiting: Bool { error == nil }
}
