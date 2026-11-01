// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import SwiftUI

/// The Orbit logo's shapes, in the coordinates of the logo artwork
/// (src/apps/res/orbit-appicon.svg): the mark spans 281...969 on both axes,
/// y pointing down
enum LogoGeometry {
    static let origin: CGFloat = 281
    static let size: CGFloat = 688
    static let center = CGPoint(x: 625, y: 627)
    static let coreWidth: CGFloat = 56
    static let moonWidth: CGFloat = 50
    static let orbitWidth: CGFloat = 58

    private static let coreRadius: CGFloat = 105
    private static let moonCenters = [CGPoint(x: 812, y: 415), CGPoint(x: 438, y: 839)]
    private static let moonRadius: CGFloat = 66
    private static let orbitRadius: CGFloat = 308
    /// Arc starts, counter-clockwise from 3 o'clock; each arc runs
    /// counter-clockwise from its round end into a ring
    private static let arcStarts: [Double] = [78, 258]
    /// Each arc runs on to the centre of its ring (the rings sit symmetrically,
    /// so one sweep fits both): its end then lies inside the ring, with no
    /// notch where a flat end would cross the ring's round edge
    private static var arcSweep: Double {
        let ring = moonCenters[1]
        let degrees = atan2(center.y - ring.y, ring.x - center.x) * 180 / .pi
        return (degrees - arcStarts[0] + 360).truncatingRemainder(dividingBy: 360)
    }

    /// The centre ring (the server), to stroke with `coreWidth`
    static var core: Path {
        circle(center, coreRadius)
    }

    /// The two orbiting rings (the clients), to stroke with `moonWidth`
    static var moons: Path {
        Path { path in
            for moon in moonCenters {
                path.addPath(circle(moon, moonRadius))
            }
        }
    }

    /// The orbit arcs with their round free ends, to fill. Each arc is one
    /// outline (no part painted twice, so translucent colours stay even); its
    /// end lies under its ring, the ring's hole cut out, so draw the rings over
    /// it, or fill `orbiting` when both have the same colour
    static var arcs: Path {
        let holes = Path { path in
            for moon in moonCenters {
                path.addPath(circle(moon, moonRadius - moonWidth / 2))
            }
        }
        return Path { path in
            for start in arcStarts {
                path.addPath(outline(from: start))
            }
        }
        .subtracting(holes)
    }

    /// The arcs and the orbiting rings as one shape, to fill when they have the
    /// same colour: no overlap to paint twice, and no seam between two
    /// antialiased edges where an arc meets its ring
    static var orbiting: Path {
        arcs.union(moons.strokedPath(StrokeStyle(lineWidth: moonWidth)))
    }

    /// An arc's outline: the outer edge, the flat end, the inner edge back,
    /// then a half circle around its start
    private static func outline(from start: Double) -> Path {
        let half = orbitWidth / 2
        let steps = 96
        let sweep = (0...steps).map { start + arcSweep * Double($0) / Double(steps) }
        return Path { path in
            path.move(to: point(at: start, radius: orbitRadius + half))
            for degrees in sweep.dropFirst() {
                path.addLine(to: point(at: degrees, radius: orbitRadius + half))
            }
            for degrees in sweep.reversed() {
                path.addLine(to: point(at: degrees, radius: orbitRadius - half))
            }
            // from the inner edge, round behind the start, to the outer edge
            let end = point(at: start)
            let radians = start * .pi / 180
            let outward = CGPoint(x: cos(radians), y: -sin(radians))
            let behind = CGPoint(x: sin(radians), y: cos(radians))
            for step in 1..<(steps / 2) {
                let phi = -Double.pi / 2 + Double.pi * Double(step) / Double(steps / 2)
                path.addLine(to: CGPoint(
                    x: end.x + half * (sin(phi) * outward.x + cos(phi) * behind.x),
                    y: end.y + half * (sin(phi) * outward.y + cos(phi) * behind.y)
                ))
            }
            path.closeSubpath()
        }
    }

    private static func circle(_ center: CGPoint, _ radius: CGFloat) -> Path {
        Path(ellipseIn: CGRect(x: center.x - radius, y: center.y - radius, width: radius * 2, height: radius * 2))
    }

    private static func point(at degrees: Double, radius: CGFloat = orbitRadius) -> CGPoint {
        let radians = degrees * .pi / 180
        return CGPoint(x: center.x + radius * cos(radians), y: center.y - radius * sin(radians))
    }
}
