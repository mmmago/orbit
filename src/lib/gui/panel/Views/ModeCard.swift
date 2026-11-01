// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

import SwiftUI

/// One of the two big choices: share this Mac's keyboard and mouse (server),
/// or use another Mac's (client). The logo lights up the part that stands for
/// the mode: the centre for the server, the orbiting rings for a client.
struct ModeCard: View {
    let mode: CoreMode
    let title: LocalizedStringKey
    let detail: LocalizedStringKey
    let isSelected: Bool
    let action: () -> Void

    @State private var isHovered = false

    var body: some View {
        Button(action: action) {
            VStack(spacing: 6) {
                LogoMark(core: highlight(for: .server), moons: highlight(for: .client), orbit: .primary)
                    .frame(width: 52, height: 52)
                    .opacity(isSelected ? 1 : (isHovered ? 0.75 : 0.45))
                Text(title)
                    .font(.callout)
                    .bold()
                    .multilineTextAlignment(.center)
                    .fixedSize(horizontal: false, vertical: true)
                Text(detail)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .padding(.vertical, 12)
            .padding(.horizontal, 8)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(background, in: .rect(cornerRadius: 12))
            .overlay {
                RoundedRectangle(cornerRadius: 12)
                    .strokeBorder(isSelected ? AnyShapeStyle(.green) : AnyShapeStyle(.separator), lineWidth: isSelected ? 2 : 1)
            }
            .overlay(alignment: .topTrailing) {
                if isSelected {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundStyle(.white, .green)
                        .padding(6)
                        .accessibilityHidden(true)
                }
            }
            .contentShape(.rect(cornerRadius: 12))
        }
        .buttonStyle(.plain)
        .onHover { isHovered = $0 }
        .accessibilityAddTraits(isSelected ? .isSelected : [])
    }

    private var background: AnyShapeStyle {
        isSelected ? AnyShapeStyle(.green.opacity(0.14)) : AnyShapeStyle(.fill.quaternary)
    }

    private func highlight(for part: CoreMode) -> Color {
        part == mode && (isSelected || isHovered) ? .green : .primary
    }
}
