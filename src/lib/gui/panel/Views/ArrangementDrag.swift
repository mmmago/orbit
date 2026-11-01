// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

import CoreGraphics

/// A screen being dragged in the arrangement
struct ArrangementDrag: Equatable {
    let computer: ArrangedComputer.ID
    let from: GridCell
    /// Frozen while dragging, so the other screens stay put
    let layout: ArrangementLayout
    var translation = CGSize.zero
    var target: GridCell
}
