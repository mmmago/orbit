// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

/// Connection state, as the status logo shows it
enum ConnectionPhase: String, Codable, Sendable {
    case idle
    case waiting
    case connecting
    case connected

    /// The orbiting rings turn while connecting and once connected
    var isTurning: Bool { self == .connecting || self == .connected }

    /// The logo is green while waiting for clients and once connected
    var isGreen: Bool { self == .waiting || self == .connected }
}
