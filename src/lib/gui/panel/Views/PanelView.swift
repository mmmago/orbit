// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

import SwiftUI

/// The menu bar panel: the server and client choice first, the options of
/// the chosen mode under it, the status at the bottom
struct PanelView: View {
    let model: PanelModel
    /// The content's height, after it changed
    let onHeightChange: (CGFloat) -> Void
    /// The content's height to come, set on the panel right before a page
    /// change, so the page is shown at the right size from its first frame
    let onPageChange: (CGFloat) -> Void

    static let width: CGFloat = 420

    @State private var height: CGFloat = 0
    @State private var settingsHeight: CGFloat = 0
    @State private var aboutHeight: CGFloat = 0

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            PageStack(shown: model.isShowingAbout ? 1 : 0) {
                VStack(alignment: .leading, spacing: 0) {
                    HeaderView()
                        .padding()
                    Divider()
                    settings
                }
                .page(isShown: !model.isShowingAbout) { settingsHeight = $0 }
                AboutView(updater: model.updater)
                    .padding()
                    .page(isShown: model.isShowingAbout) { aboutHeight = $0 }
            }
            Divider()
            FooterView(model: model)
                .padding(.horizontal)
                .padding(.vertical, 10)
        }
        .overlay(alignment: .topTrailing) {
            aboutButton
                .padding(10)
        }
        .frame(width: Self.width)
        .fixedSize(horizontal: false, vertical: true)
        .onGeometryChange(for: CGFloat.self) { proxy in
            proxy.size.height
        } action: { newHeight in
            height = newHeight
            onHeightChange(newHeight)
        }
        // until the panel has its new height, the content stays at the top,
        // under the menu bar, instead of being centred
        .frame(maxHeight: .infinity, alignment: .top)
    }

    /// "?" opens the About page; on it, the arrow goes back to the settings.
    /// One button, so it keeps its size and place when its symbol changes.
    private var aboutButton: some View {
        let isShowingAbout = model.isShowingAbout
        return Button(action: toggleAbout) {
            if isShowingAbout {
                Label("Back to the Settings", systemImage: "chevron.left")
            } else {
                Label("About Orbit", systemImage: "questionmark")
            }
        }
        .buttonStyle(.roundSymbol)
        .help(isShowingAbout ? "Back to the settings" : "About Orbit")
    }

    /// The server and client choice, and the options of the chosen mode
    private var settings: some View {
        VStack(alignment: .leading, spacing: 16) {
            ModeSelector(selection: model.state.mode, onSelect: model.selectMode)
            switch model.state.mode {
            case .server:
                ServerSettingsView(model: model)
            case .client:
                ClientSettingsView(model: model)
            case .none:
                Text("Choose whether this Mac shares its keyboard and mouse, or uses another Mac's.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
        }
        .padding()
    }

    private func toggleAbout() {
        let showingAbout = model.isShowingAbout
        let shown = showingAbout ? aboutHeight : settingsHeight
        let next = showingAbout ? settingsHeight : aboutHeight
        if height > 0, shown > 0, next > 0 {
            onPageChange(height - shown + next)
        }
        model.isShowingAbout = !showingAbout
    }
}
