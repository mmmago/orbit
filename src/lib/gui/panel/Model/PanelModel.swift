// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import Foundation
import Observation
import SwiftUI

/// The panel's state and the actions it sends back to Orbit
@MainActor
@Observable
final class PanelModel {
    private(set) var state = PanelState()
    let updater = ReleaseUpdater()

    @ObservationIgnored var send: (PanelAction) -> Void = { _ in }

    /// The About page is shown instead of the settings; the panel opens on
    /// the settings again
    var isShowingAbout = false

    /// Replaces the state with the JSON Orbit sent
    func apply(json: String) {
        guard let decoded = try? JSONDecoder().decode(PanelState.self, from: Data(json.utf8)) else { return }
        if decoded != state {
            state = decoded
        }
    }

    func selectMode(_ mode: CoreMode) {
        guard state.mode != mode else { return }
        state.mode = mode
        send(.selectMode(mode))
    }

    /// A switch bound to a server or client option; Orbit applies and saves it
    func option(_ key: String, _ path: WritableKeyPath<PanelState, Bool>) -> Binding<Bool> {
        Binding(
            get: { self.state[keyPath: path] },
            set: { value in
                self.state[keyPath: path] = value
                self.send(.setOption(key, value))
            }
        )
    }

    func pickServer(_ address: String) {
        guard address != state.selectedServer || state.phase == .idle else { return }
        send(.pickServer(address: address))
    }

    func moveComputer(from: GridCell, to: GridCell) {
        guard from != to else { return }
        send(.moveComputer(from: from, to: to))
    }

    /// Stores this Mac's password, checked first, for the server's Touch ID
    func setScreenUnlockPassword(_ password: String) {
        send(.setScreenUnlockPassword(password))
    }

    func removeScreenUnlockPassword() {
        state.client.screenUnlock = false
        send(.removeScreenUnlockPassword)
    }

    func restart() {
        send(.restart)
    }

    func quit() {
        send(.quit)
    }
}
