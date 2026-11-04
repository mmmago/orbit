// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import SwiftUI

/// Server mode: the arrangement of the computers and the sharing options
struct ServerSettingsView: View {
    let model: PanelModel

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            SectionTitle(
                title: "Arrange computers",
                detail: "Drag the screens to place the computers. The cursor goes across the edges they share."
            )
            ArrangementCanvas(arrangement: model.state.arrangement, onMove: model.moveComputer)
                .background(.fill.quaternary, in: .rect(cornerRadius: 10))

            // another Mac in server mode can't connect here: set it as client
            if !model.state.servers.isEmpty {
                SectionTitle(
                    title: "Other servers",
                    detail: "These Macs are in server mode too. Set one as client to use it from here."
                )
                .padding(.top, 8)
                SectionCard {
                    OtherServerList(model: model)
                }
            }

            SectionTitle(title: "Options")
                .padding(.top, 8)
            SectionCard {
                OptionToggle(
                    title: "Trackpad gestures",
                    detail: "Swipes, pinch, rotate and smooth scrolling on the other Mac",
                    isOn: model.option("trackpadGestures", \.server.trackpadGestures)
                )
                Divider()
                OptionToggle(
                    title: "Drag and drop",
                    detail: "Drag files and folders from one Mac to the other",
                    isOn: model.option("fileDragDrop", \.server.fileDragDrop)
                )
                Divider()
                OptionToggle(
                    title: "Clipboard sharing",
                    detail: "Copy text on one Mac, paste it on the other",
                    isOn: model.option("clipboard", \.server.clipboard)
                )
                Divider()
                OptionToggle(
                    title: "Touch ID permission in menu bar",
                    detail: "To unlock a locked client, the menu bar icon becomes the Touch ID glyph instead of showing a panel",
                    isOn: model.option("touchIdInMenuBar", \.server.touchIdInMenuBar)
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
