// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import SwiftUI

/// The small logo next to the status text: green while waiting or connected,
/// with the orbiting rings turning while connecting or connected
struct StatusLogo: View {
    let phase: ConnectionPhase

    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    private static let secondsPerTurn = 2.4

    var body: some View {
        TimelineView(.animation(paused: !phase.isTurning || reduceMotion)) { timeline in
            let color: Color = phase.isGreen ? .green : .primary
            LogoMark(core: color, moons: color, orbit: color, rotation: rotation(at: timeline.date))
        }
        .frame(width: 18, height: 18)
        .accessibilityHidden(true)
    }

    private func rotation(at date: Date) -> Angle {
        guard phase.isTurning, !reduceMotion else { return .zero }
        let progress = date.timeIntervalSinceReferenceDate.truncatingRemainder(dividingBy: Self.secondsPerTurn)
        // counter-clockwise, so each ring leads and its arc trails behind it
        return .degrees(-360 * progress / Self.secondsPerTurn)
    }
}
