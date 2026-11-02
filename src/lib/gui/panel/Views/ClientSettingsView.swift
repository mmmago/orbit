// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import SwiftUI

/// Client mode: the server to connect to, among those found on the
/// network, and the scrolling options
struct ClientSettingsView: View {
    let model: PanelModel

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            SectionTitle(title: "Connect to")
            SectionCard {
                ServerPicker(model: model)
            }

            SectionTitle(title: "Options")
                .padding(.top, 8)
            SectionCard {
                OptionToggle(
                    title: "Invert horizontal scroll",
                    isOn: model.option("invertHorizontalScroll", \.client.invertHorizontalScroll)
                )
                Divider()
                OptionToggle(
                    title: "Invert vertical scroll",
                    isOn: model.option("invertVerticalScroll", \.client.invertVerticalScroll)
                )
                Divider()
                OptionToggle(
                    title: "Animated menu bar icon",
                    detail: "Shows the connection state, like the logo at the bottom",
                    isOn: model.option("animatedMenuBarIcon", \.animatedMenuBarIcon)
                )
            }

            SectionTitle(title: "Lock screen")
                .padding(.top, 8)
            SectionCard {
                ScreenUnlockOption(model: model)
            }
        }
    }
}

/// Unlocking this Mac with Touch ID on the server: the switch asks for this
/// Mac's password once, kept in its Keychain
private struct ScreenUnlockOption: View {
    let model: PanelModel
    @State private var isEntering = false
    @State private var password = ""
    @FocusState private var isPasswordFocused: Bool

    private var stored: Bool { model.state.client.screenUnlock }

    private var isOn: Binding<Bool> {
        Binding(
            get: { stored || isEntering },
            set: { on in
                password = ""
                isEntering = on
                if on {
                    isPasswordFocused = true
                } else {
                    model.removeScreenUnlockPassword()
                }
            }
        )
    }

    var body: some View {
        OptionToggle(
            title: "Unlock with the server's Touch ID",
            detail: "When the cursor comes here while this Mac is locked, touch the server's Touch ID: this Mac types its password, which never leaves it. Needs TLS.",
            isOn: isOn
        )
        if isEntering && !stored {
            Divider()
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    SecureField("Password of this Mac", text: $password)
                        .textFieldStyle(.roundedBorder)
                        .focused($isPasswordFocused)
                        .onSubmit(save)
                    Button("Save", action: save)
                        .disabled(password.isEmpty)
                }
                if let error = model.state.client.screenUnlockError {
                    Text(error)
                        .font(.caption)
                        .foregroundStyle(.red)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            .padding(.vertical, 8)
        }
    }

    private func save() {
        guard !password.isEmpty else { return }
        model.setScreenUnlockPassword(password)
        password = ""
    }
}
