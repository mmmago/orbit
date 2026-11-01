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
        }
    }
}
