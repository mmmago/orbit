// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

import Foundation

/// What the panel asks Orbit to do, sent as a name and JSON arguments
enum PanelAction: Sendable {
    case selectMode(CoreMode)
    case setOption(String, Bool)
    case pickServer(address: String)
    case moveComputer(from: GridCell, to: GridCell)
    case restart
    case quit

    var name: String {
        switch self {
        case .selectMode: "selectMode"
        case .setOption: "setOption"
        case .pickServer: "pickServer"
        case .moveComputer: "moveComputer"
        case .restart: "restart"
        case .quit: "quit"
        }
    }

    /// Arguments as a JSON object
    var argumentsJSON: String {
        let arguments: [String: Any] = switch self {
        case .selectMode(let mode): ["mode": mode.rawValue]
        case .setOption(let key, let value): ["key": key, "value": value]
        case .pickServer(let address): ["address": address]
        case .moveComputer(let from, let to):
            ["fromColumn": from.column, "fromRow": from.row, "toColumn": to.column, "toRow": to.row]
        case .restart, .quit: [:]
        }
        guard let data = try? JSONSerialization.data(withJSONObject: arguments) else { return "{}" }
        return String(decoding: data, as: UTF8.self)
    }
}
