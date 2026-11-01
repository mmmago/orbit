// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import SwiftUI

/// The Orbit logo: a centre ring (the server), two orbiting rings (the
/// clients) and the orbit arcs (see LogoGeometry)
struct LogoMark: View {
    var core: Color
    var moons: Color
    var orbit: Color
    /// Turns the arcs and orbiting rings around the centre
    var rotation = Angle.zero

    var body: some View {
        Canvas { context, size in
            let scale = min(size.width, size.height) / LogoGeometry.size
            context.translateBy(
                x: (size.width - LogoGeometry.size * scale) / 2,
                y: (size.height - LogoGeometry.size * scale) / 2
            )
            context.scaleBy(x: scale, y: scale)
            context.translateBy(x: -LogoGeometry.origin, y: -LogoGeometry.origin)

            context.stroke(LogoGeometry.core, with: .color(core), lineWidth: LogoGeometry.coreWidth)

            let center = LogoGeometry.center
            context.translateBy(x: center.x, y: center.y)
            context.rotate(by: rotation)
            context.translateBy(x: -center.x, y: -center.y)

            if moons == orbit {
                context.fill(LogoGeometry.orbiting, with: .color(orbit))
            } else {
                // the rings (an opaque highlight) cover the arcs' flat ends
                context.fill(LogoGeometry.arcs, with: .color(orbit))
                context.stroke(LogoGeometry.moons, with: .color(moons), lineWidth: LogoGeometry.moonWidth)
            }
        }
        .drawingGroup()
    }
}

#Preview {
    LogoMark(core: .green, moons: .primary, orbit: .primary)
        .frame(width: 120, height: 120)
        .padding()
}
