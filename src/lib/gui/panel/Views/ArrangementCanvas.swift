// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import SwiftUI

/// The computers arranged like displays in macOS: drag a screen to move it.
/// It snaps to a grid position, swapping with the computer already there.
struct ArrangementCanvas: View {
    let arrangement: Arrangement
    let onMove: (GridCell, GridCell) -> Void

    @State private var size = CGSize.zero
    @State private var drag: ArrangementDrag?

    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    var body: some View {
        let computers = arrangement.shownComputers
        let layout = drag?.layout ?? ArrangementLayout(computers: computers, in: size)

        ZStack(alignment: .topLeading) {
            if let drag {
                let target = layout.frame(for: drag.target)
                RoundedRectangle(cornerRadius: 6)
                    .strokeBorder(.secondary, style: StrokeStyle(lineWidth: 1.5, dash: [5, 3]))
                    .frame(width: target.width - 4, height: target.height - 4)
                    .position(x: target.midX, y: target.midY)
            }
            ForEach(computers) { computer in
                let frame = layout.frame(for: computer.cell)
                let isDragged = drag?.computer == computer.id
                let offset = isDragged ? drag?.translation ?? .zero : .zero
                ScreenTile(computer: computer, isLifted: isDragged)
                    .frame(width: frame.width, height: frame.height)
                    .position(x: frame.midX + offset.width, y: frame.midY + offset.height)
                    .zIndex(isDragged ? 1 : 0)
                    .gesture(dragGesture(for: computer, layout: layout))
                    .accessibilityElement(children: .ignore)
                    .accessibilityLabel(computer.name)
                    .accessibilityValue(computer.isServer ? Text("Server") : Text("Client"))
                    .accessibilityAction(named: "Move left") { move(computer, columns: -1, rows: 0) }
                    .accessibilityAction(named: "Move right") { move(computer, columns: 1, rows: 0) }
                    .accessibilityAction(named: "Move up") { move(computer, columns: 0, rows: -1) }
                    .accessibilityAction(named: "Move down") { move(computer, columns: 0, rows: 1) }
            }
        }
        .frame(maxWidth: .infinity)
        .frame(height: 210)
        .onGeometryChange(for: CGSize.self) { proxy in
            proxy.size
        } action: { newSize in
            size = newSize
        }
        .animation(reduceMotion ? nil : .snappy, value: arrangement)
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Computer arrangement")
    }

    private func dragGesture(for computer: ArrangedComputer, layout: ArrangementLayout) -> some Gesture {
        DragGesture(minimumDistance: 3)
            .onChanged { value in
                var current = drag ?? ArrangementDrag(computer: computer.id, from: computer.cell, layout: layout, target: computer.cell)
                current.translation = value.translation
                let frame = current.layout.frame(for: computer.cell)
                let center = CGPoint(x: frame.midX + value.translation.width, y: frame.midY + value.translation.height)
                current.target = current.layout.cell(at: center, columns: arrangement.columns, rows: arrangement.rows)
                drag = current
            }
            .onEnded { _ in
                if let drag {
                    onMove(drag.from, drag.target)
                }
                drag = nil
            }
    }

    private func move(_ computer: ArrangedComputer, columns: Int, rows: Int) {
        let target = GridCell(
            column: min(max(computer.column + columns, 0), arrangement.columns - 1),
            row: min(max(computer.row + rows, 0), arrangement.rows - 1)
        )
        onMove(computer.cell, target)
    }
}
