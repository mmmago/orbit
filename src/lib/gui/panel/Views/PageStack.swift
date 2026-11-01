// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

import SwiftUI

/// Pages on top of each other, as tall as the shown one. Every page stays
/// laid out at its own height, so its height is known before it is shown and
/// its state survives while another page is shown.
struct PageStack: Layout {
    var shown: Int

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        guard subviews.indices.contains(shown) else { return .zero }
        let size = subviews[shown].sizeThatFits(ProposedViewSize(width: proposal.width, height: nil))
        return CGSize(width: proposal.width ?? size.width, height: size.height)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        for subview in subviews {
            subview.place(at: bounds.origin, anchor: .topLeading, proposal: ProposedViewSize(width: bounds.width, height: nil))
        }
    }
}

extension View {
    /// A page of a `PageStack`: invisible and out of reach while another
    /// page is shown; `onHeight` gets its height either way
    func page(isShown: Bool, onHeight: @escaping (CGFloat) -> Void) -> some View {
        opacity(isShown ? 1 : 0)
            .allowsHitTesting(isShown)
            .disabled(!isShown)
            .accessibilityHidden(!isShown)
            .onGeometryChange(for: CGFloat.self) { proxy in
                proxy.size.height
            } action: { height in
                onHeight(height)
            }
    }
}
