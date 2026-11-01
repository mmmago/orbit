// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

import AppKit
import QuartzCore
import SwiftUI

/// The logo in the menu bar, drawn with Core Animation layers: when turning,
/// the arcs and orbiting rings rotate around the centre at the display's
/// refresh rate, animated by the window server, so it is smooth and cheap
final class StatusItemLogoView: NSView {
    var isGreen = false {
        didSet { if isGreen != oldValue { updateColors() } }
    }

    var isTurning = false {
        didSet { if isTurning != oldValue { updateAnimation() } }
    }

    private let core = CAShapeLayer()
    private let orbit = CALayer()
    /// The arcs and orbiting rings, one shape in one colour
    private let orbiting = CAShapeLayer()

    private static let spinKey = "spin"
    private static let secondsPerTurn: CFTimeInterval = 2.4

    init(side: CGFloat) {
        super.init(frame: NSRect(x: 0, y: 0, width: side, height: side))
        wantsLayer = true
        buildLayers(side: side)
        updateColors()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    /// Clicks go to the status item's button
    override func hitTest(_ point: NSPoint) -> NSView? {
        nil
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        updateColors()
    }

    private func buildLayers(side: CGFloat) {
        guard let root = layer else { return }
        let scale = side / LogoGeometry.size
        // logo coordinates (y down) to the layer's (y up)
        var transform = CGAffineTransform(
            a: scale, b: 0, c: 0, d: -scale,
            tx: -LogoGeometry.origin * scale,
            ty: side + LogoGeometry.origin * scale
        )
        func path(_ shape: Path) -> CGPath? {
            shape.cgPath.copy(using: &transform)
        }

        core.path = path(LogoGeometry.core)
        core.lineWidth = LogoGeometry.coreWidth * scale
        core.fillColor = nil

        orbiting.path = path(LogoGeometry.orbiting)

        let bounds = CGRect(x: 0, y: 0, width: side, height: side)
        for shape in [core, orbiting] {
            shape.frame = bounds
            shape.contentsScale = 2
        }
        orbit.frame = bounds
        orbit.addSublayer(orbiting)
        // turn around the logo's centre
        let center = LogoGeometry.center.applying(transform)
        orbit.anchorPoint = CGPoint(x: center.x / side, y: center.y / side)
        orbit.position = center
        root.addSublayer(core)
        root.addSublayer(orbit)
    }

    private func updateColors() {
        var neutral = NSColor.black.cgColor
        var green = NSColor.black.cgColor
        effectiveAppearance.performAsCurrentDrawingAppearance {
            neutral = NSColor.labelColor.cgColor
            green = NSColor.systemGreen.cgColor
        }
        let color = isGreen ? green : neutral
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        core.strokeColor = color
        orbiting.fillColor = color
        CATransaction.commit()
    }

    private func updateAnimation() {
        if isTurning {
            guard orbit.animation(forKey: Self.spinKey) == nil else { return }
            let spin = CABasicAnimation(keyPath: "transform.rotation.z")
            spin.fromValue = 0
            // counter-clockwise, so each ring leads and its arc trails behind it
            spin.toValue = 2 * Double.pi
            spin.duration = Self.secondsPerTurn
            spin.repeatCount = .infinity
            spin.isRemovedOnCompletion = false
            orbit.add(spin, forKey: Self.spinKey)
        } else {
            orbit.removeAnimation(forKey: Self.spinKey)
        }
    }
}
