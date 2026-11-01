// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

/// The server's grid of computers
struct Arrangement: Codable, Equatable, Sendable {
    var columns = 5
    var rows = 3
    var computers: [ArrangedComputer] = []

    var shownComputers: [ArrangedComputer] { computers.filter(\.isShown) }
}
