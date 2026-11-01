// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

import CoreGraphics

/// Where each computer's screen goes in the arrangement area: the shown
/// computers are centred, screens of neighbouring cells touch
struct ArrangementLayout: Equatable {
    let minColumn: Int
    let minRow: Int
    let tile: CGSize
    let origin: CGPoint

    private static let padding: CGFloat = 16
    private static let maxTileWidth: CGFloat = 150
    /// Room left around the arrangement to drop a screen onto, in screens
    private static let dropMargin: CGFloat = 0.6
    private static let aspect: CGFloat = 1.6

    init(computers: [ArrangedComputer], in size: CGSize) {
        let columns = computers.map(\.column)
        let rows = computers.map(\.row)
        let minColumn = columns.min() ?? 0
        let minRow = rows.min() ?? 0
        let spanColumns = (columns.max() ?? 0) - minColumn + 1
        let spanRows = (rows.max() ?? 0) - minRow + 1

        // leave room around the arrangement to drop a screen onto
        let width = size.width - 2 * Self.padding
        let height = size.height - 2 * Self.padding
        let tileWidth = max(40, min(
            Self.maxTileWidth,
            width / (CGFloat(spanColumns) + Self.dropMargin),
            height / (CGFloat(spanRows) + Self.dropMargin) * Self.aspect
        ))
        let tile = CGSize(width: tileWidth, height: tileWidth / Self.aspect)

        self.minColumn = minColumn
        self.minRow = minRow
        self.tile = tile
        self.origin = CGPoint(
            x: (size.width - CGFloat(spanColumns) * tile.width) / 2,
            y: (size.height - CGFloat(spanRows) * tile.height) / 2
        )
    }

    func frame(for cell: GridCell) -> CGRect {
        CGRect(
            x: origin.x + CGFloat(cell.column - minColumn) * tile.width,
            y: origin.y + CGFloat(cell.row - minRow) * tile.height,
            width: tile.width,
            height: tile.height
        )
    }

    /// The grid cell under a point, kept inside the grid
    func cell(at point: CGPoint, columns: Int, rows: Int) -> GridCell {
        let column = minColumn + Int(((point.x - origin.x) / tile.width).rounded(.down))
        let row = minRow + Int(((point.y - origin.y) / tile.height).rounded(.down))
        return GridCell(column: min(max(column, 0), columns - 1), row: min(max(row, 0), rows - 1))
    }
}
