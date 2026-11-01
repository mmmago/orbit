// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

import SwiftUI

/// A round button drawn like macOS' help button, for any symbol: buttons
/// that swap their symbol keep the same size and place
struct RoundSymbolButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        RoundSymbolButton(configuration: configuration)
    }
}

extension ButtonStyle where Self == RoundSymbolButtonStyle {
    static var roundSymbol: RoundSymbolButtonStyle { RoundSymbolButtonStyle() }
}

private struct RoundSymbolButton: View {
    let configuration: ButtonStyleConfiguration

    @Environment(\.isEnabled) private var isEnabled

    var body: some View {
        configuration.label
            .labelStyle(.iconOnly)
            .font(.system(size: 13, weight: .medium))
            .foregroundStyle(isEnabled ? .primary : .tertiary)
            .frame(width: 24, height: 24)
            // the help button's fill, a step darker while pressed
            .background(configuration.isPressed ? AnyShapeStyle(.fill) : AnyShapeStyle(.fill.secondary), in: .circle)
            .contentShape(.circle)
    }
}
