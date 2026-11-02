// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import AppKit
import LocalAuthentication
import LocalAuthenticationEmbeddedUI
import SwiftUI

/// Shows the menu bar panel. Used from Objective-C++ (gui/OrbitPanel.mm):
/// Orbit sends the state as JSON with `update(_:)`, and gets the panel's
/// actions back through `onAction` as a name and JSON arguments.
@MainActor
@objc(OrbitPanelController)
public final class OrbitPanelController: NSObject {
    @objc public var onAction: ((String, String) -> Void)?
    /// The answer to `beginTouchId(_:reason:)`: its nonce, and whether the
    /// user touched Touch ID
    @objc public var onTouchIdResult: ((String, Bool) -> Void)?

    private let model = PanelModel()
    private var panel: OrbitPanelWindow?
    private var hiddenAt = Date.distantPast
    private var contentHeight: CGFloat = 0
    private var isResizePending = false
    private var outsideClickMonitor: Any?
    private var statusItem: NSStatusItem?
    private var statusLogo: StatusItemLogoView?
    private var accessibilityObserver: (any NSObjectProtocol)?
    private var deactivationObserver: (any NSObjectProtocol)?
    /// Touch ID shown in place of the logo, to unlock a client
    private var touchId: (nonce: String, context: LAContext, overlay: NSPanel, symbol: TouchIdSymbolView)?

    /// A click on the menu bar icon closes the panel before it toggles it;
    /// within this delay, the toggle doesn't reopen it
    private static let reopenDelay: TimeInterval = 0.3

    @objc override public init() {
        super.init()
        model.send = { [weak self] action in
            self?.onAction?(action.name, action.argumentsJSON)
        }
        model.updater.quit = { [weak self] in
            self?.model.quit()
        }
    }

    @objc public func update(_ json: String) {
        model.apply(json: json)
        refreshStatusItem()
    }

    /// Puts Orbit's icon in the menu bar; clicking it opens the panel
    @objc public func installStatusItem() {
        guard statusItem == nil else { return }
        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        guard let button = item.button else { return }
        button.target = self
        button.action = #selector(statusItemClicked)
        button.setAccessibilityTitle("Orbit")
        button.toolTip = "Orbit"

        let logo = StatusItemLogoView(side: 18)
        logo.frame.origin = NSPoint(x: (button.bounds.width - 18) / 2, y: (button.bounds.height - 18) / 2)
        logo.autoresizingMask = [.minXMargin, .maxXMargin, .minYMargin, .maxYMargin]
        button.addSubview(logo)

        statusItem = item
        statusLogo = logo
        accessibilityObserver = NSWorkspace.shared.notificationCenter.addObserver(
            forName: NSWorkspace.accessibilityDisplayOptionsDidChangeNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated {
                self?.refreshStatusItem()
            }
        }
        refreshStatusItem()
        model.updater.start()
    }

    /// The server asks to unlock a locked client: the menu bar icon becomes
    /// the Touch ID glyph until the user touches the sensor, or the request
    /// is cancelled (the cursor left that client)
    @objc(beginTouchId:reason:)
    public func beginTouchId(_ nonce: String, reason: String) {
        endTouchId()
        removeLeavingGlyph()
        let context = LAContext()
        // Touch ID only: no password
        context.localizedFallbackTitle = ""
        var error: NSError?
        guard let button = statusItem?.button,
              context.canEvaluatePolicy(.deviceOwnerAuthenticationWithBiometrics, error: &error)
        else {
            onTouchIdResult?(nonce, false)
            return
        }

        // Touch ID ignores the finger in the status item's own window, which
        // can never be key: the glyph goes in a small transparent panel of
        // ours that can, right over the icon (without activating Orbit)
        let overlay = TouchIdOverlayPanel(over: button, side: Self.touchIdSide + 2 * Self.touchIdMargin)
        // menu bar colours, which follow the wallpaper, not the app
        overlay.appearance = button.effectiveAppearance
        // Apple's glyph is what lets Touch ID answer here rather than in the
        // system dialog. It only listens while it is opaque, of a real size
        // and in a window on screen, so it keeps all that, and a container
        // with an empty mask keeps it from being drawn: the plainer glyph
        // above it is what shows
        let glyph = LAAuthenticationView(context: context, controlSize: .small)
        let maskedGlyph = NSView()
        maskedGlyph.wantsLayer = true
        maskedGlyph.layer?.mask = CALayer()
        let symbol = TouchIdSymbolView(side: Self.touchIdSide)
        if let content = overlay.contentView {
            content.wantsLayer = true
            // sized by constraints (a frame alone draws Apple's glyph at its
            // own size from the corner)
            func center(_ view: NSView, in parent: NSView) {
                view.translatesAutoresizingMaskIntoConstraints = false
                parent.addSubview(view)
                NSLayoutConstraint.activate([
                    view.centerXAnchor.constraint(equalTo: parent.centerXAnchor),
                    view.centerYAnchor.constraint(equalTo: parent.centerYAnchor),
                    view.widthAnchor.constraint(equalToConstant: Self.touchIdSide),
                    view.heightAnchor.constraint(equalToConstant: Self.touchIdSide),
                ])
            }
            center(maskedGlyph, in: content)
            center(glyph, in: maskedGlyph)
            center(symbol, in: content)
            content.layoutSubtreeIfNeeded()
        }
        let title = "Touch ID to \(reason)"
        button.toolTip = title
        button.setAccessibilityTitle(title)
        touchId = (nonce, context, overlay, symbol)

        // the logo fades out and shrinks as the glyph fades in and grows
        overlay.alphaValue = 0
        Self.zoom(overlay.contentView?.layer, shown: false, animated: false)
        overlay.orderFrontRegardless()
        overlay.makeKey()
        Self.zoom(statusLogo?.layer, shown: false, animated: true)
        Self.fade(statusLogo?.layer, shown: false)
        Self.zoom(overlay.contentView?.layer, shown: true, animated: true)
        NSAnimationContext.runAnimationGroup { animation in
            animation.duration = Self.swapDuration
            animation.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            overlay.animator().alphaValue = 1
        }

        context.evaluatePolicy(.deviceOwnerAuthenticationWithBiometrics, localizedReason: reason) { [weak self] success, _ in
            Task { @MainActor in
                guard let self, self.touchId?.nonce == nonce else { return }
                // the client unlocks now; the glyph fills, and stays a moment
                self.onTouchIdResult?(nonce, success)
                if success {
                    self.touchId?.symbol.fill()
                }
                self.endTouchId(holding: success ? TouchIdSymbolView.fillDuration + Self.successHold : 0)
            }
        }
    }

    @objc(cancelTouchId:)
    public func cancelTouchId(_ nonce: String) {
        guard touchId?.nonce == nonce else { return }
        endTouchId()
    }

    /// The menu bar glyph's side, the logo's
    private static let touchIdSide: CGFloat = 18
    /// Room around it: the glyph draws a little outside its frame
    private static let touchIdMargin: CGFloat = 4
    /// Logo ⇄ glyph
    private static let swapDuration: CFTimeInterval = 0.25
    /// The size the leaving icon shrinks to, and the coming one grows from
    private static let swapScale: CGFloat = 0.5
    /// How long the filled glyph stays before the logo comes back
    private static let successHold: TimeInterval = 1

    /// The glyph on its way out (held after a success, then fading), and
    /// what tells a newer request apart from it
    private var leavingGlyph: (overlay: NSPanel, context: LAContext)?
    private var touchIdGeneration = 0

    /// Back to the logo, after \p holding seconds
    private func endTouchId(holding: TimeInterval = 0) {
        guard let current = touchId else { return }
        touchId = nil
        statusItem?.button?.toolTip = "Orbit"
        statusItem?.button?.setAccessibilityTitle("Orbit")
        if holding == 0 {
            // stop listening to the sensor at once
            current.context.invalidate()
        }
        leavingGlyph = (current.overlay, current.context)
        touchIdGeneration += 1
        let generation = touchIdGeneration
        DispatchQueue.main.asyncAfter(deadline: .now() + holding) { [weak self] in
            MainActor.assumeIsolated {
                guard let self, self.touchIdGeneration == generation else { return }
                self.swapBackToLogo(from: current.overlay, context: current.context)
            }
        }
    }

    private func swapBackToLogo(from overlay: NSPanel, context: LAContext) {
        context.invalidate()
        Self.zoom(overlay.contentView?.layer, shown: false, animated: true)
        NSAnimationContext.runAnimationGroup { animation in
            animation.duration = Self.swapDuration
            animation.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            overlay.animator().alphaValue = 0
        } completionHandler: {
            MainActor.assumeIsolated {
                overlay.orderOut(nil)
            }
        }
        Self.zoom(statusLogo?.layer, shown: true, animated: true)
        Self.fade(statusLogo?.layer, shown: true)
        if leavingGlyph?.overlay === overlay {
            leavingGlyph = nil
        }
    }

    /// A new request comes while the last glyph is still leaving: it goes at
    /// once, the logo staying hidden for the new glyph
    private func removeLeavingGlyph() {
        touchIdGeneration += 1
        guard let leaving = leavingGlyph else { return }
        leavingGlyph = nil
        leaving.context.invalidate()
        leaving.overlay.orderOut(nil)
    }

    /// Scale \p layer's content around its centre: full size when shown,
    /// smaller when hidden (not with Reduce motion: then it only fades)
    private static func zoom(_ layer: CALayer?, shown: Bool, animated: Bool) {
        guard let layer else { return }
        let reduceMotion = NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
        let scale = shown || reduceMotion ? 1 : swapScale
        let bounds = layer.bounds
        var transform = CATransform3DMakeTranslation(bounds.midX, bounds.midY, 0)
        transform = CATransform3DScale(transform, scale, scale, 1)
        transform = CATransform3DTranslate(transform, -bounds.midX, -bounds.midY, 0)
        animate(layer, "sublayerTransform", to: NSValue(caTransform3D: transform), animated: animated)
    }

    private static func fade(_ layer: CALayer?, shown: Bool) {
        guard let layer else { return }
        animate(layer, "opacity", to: NSNumber(value: shown ? 1 : 0), animated: true)
    }

    /// Views' layers don't animate by themselves: an explicit animation from
    /// what is on screen now, then the final value
    private static func animate(_ layer: CALayer, _ key: String, to value: Any, animated: Bool) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        if animated {
            let animation = CABasicAnimation(keyPath: key)
            animation.fromValue = layer.presentation()?.value(forKeyPath: key) ?? layer.value(forKeyPath: key)
            animation.toValue = value
            animation.duration = swapDuration
            animation.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            layer.add(animation, forKey: key)
        }
        layer.setValue(value, forKeyPath: key)
        CATransaction.commit()
    }

    @objc private func statusItemClicked() {
        toggle()
    }

    /// Plain logo, or animated with the connection state when the option is on
    private func refreshStatusItem() {
        guard let statusLogo else { return }
        let animated = model.state.animatedMenuBarIcon
        let phase = model.state.phase
        statusLogo.isGreen = animated && phase.isGreen
        statusLogo.isTurning = animated && phase.isTurning
            && !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
    }

    @objc public var isShown: Bool {
        panel?.isVisible ?? false
    }

    @objc public func toggle() {
        if isShown {
            hide()
        } else if Date().timeIntervalSince(hiddenAt) > Self.reopenDelay {
            // after the click on the menu bar icon has been handled
            DispatchQueue.main.async { [weak self] in
                self?.show()
            }
        }
    }

    @objc public func show() {
        let panel = panel ?? makePanel()
        self.panel = panel
        // the settings page, back after the About page was left open: lay
        // it out and take its size before the panel shows
        panel.contentView?.layoutSubtreeIfNeeded()
        if isResizePending {
            applyContentHeight()
        }
        place(panel)
        NSApp.activate()
        panel.makeKeyAndOrderFront(nil)
        watchForDismissal()
    }

    @objc public func hide() {
        stopWatchingForDismissal()
        guard let panel, panel.isVisible else { return }
        panel.orderOut(nil)
        hiddenAt = Date()
        model.isShowingAbout = false
    }

    /// Like a menu: close on a click in another app, or when another app
    /// becomes active. (Not on losing key status: the click on the menu bar
    /// icon that opens the panel can take it away for a moment.)
    private func watchForDismissal() {
        guard outsideClickMonitor == nil else { return }
        outsideClickMonitor = NSEvent.addGlobalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown]) { [weak self] _ in
            MainActor.assumeIsolated {
                self?.hide()
            }
        }
        deactivationObserver = NotificationCenter.default.addObserver(
            forName: NSApplication.didResignActiveNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated {
                self?.hide()
            }
        }
    }

    private func stopWatchingForDismissal() {
        if let outsideClickMonitor {
            NSEvent.removeMonitor(outsideClickMonitor)
        }
        outsideClickMonitor = nil
        if let deactivationObserver {
            NotificationCenter.default.removeObserver(deactivationObserver)
        }
        deactivationObserver = nil
    }

    private func makePanel() -> OrbitPanelWindow {
        let panel = OrbitPanelWindow(contentRect: NSRect(x: 0, y: 0, width: PanelView.width, height: 480))
        panel.onCancel = { [weak self] in
            self?.hide()
        }

        let background = NSVisualEffectView()
        background.material = .popover
        background.blendingMode = .behindWindow
        background.state = .active

        let hosting = NSHostingView(rootView: PanelView(
            model: model,
            onHeightChange: { [weak self] height in
                self?.resize(to: height)
            },
            onPageChange: { [weak self] height in
                self?.resizeBeforePageChange(to: height)
            }
        ))
        // the hidden title bar must not push the content down
        hosting.safeAreaRegions = []
        // follows the panel's size at once when it is resized, without
        // waiting for a layout pass
        hosting.autoresizingMask = [.width, .height]
        background.addSubview(hosting)
        panel.contentView = background
        hosting.frame = background.bounds

        // size the panel to its content once, then follow the content's
        // height ourselves so the panel grows downwards from the menu bar
        contentHeight = hosting.fittingSize.height
        hosting.sizingOptions = []
        panel.setContentSize(NSSize(width: PanelView.width, height: contentHeight))
        return panel
    }

    /// Called by SwiftUI while it lays the content out: resizing the panel
    /// right then leaves the content placed for the old size (shifted up or
    /// down when switching between server and client), so the panel is
    /// resized just after, once
    private func resize(to height: CGFloat) {
        guard abs(height - contentHeight) > 0.5 else { return }
        contentHeight = height
        guard !isResizePending else { return }
        isResizePending = true
        DispatchQueue.main.async { [weak self] in
            self?.applyContentHeight()
        }
    }

    /// Grows or shrinks the panel downwards, its top staying under the menu bar
    private func applyContentHeight() {
        isResizePending = false
        guard setPanelFrame(display: true) else { return }
        panel?.contentView?.layoutSubtreeIfNeeded()
    }

    /// Called when a button switches pages, before the new page is laid
    /// out: the panel takes its size now, without drawing, so the new page
    /// and the new size reach the screen together, in one frame
    private func resizeBeforePageChange(to height: CGFloat) {
        contentHeight = height
        setPanelFrame(display: false)
    }

    /// The panel's frame for `contentHeight`, its top where it is; false
    /// when it already had it
    @discardableResult
    private func setPanelFrame(display: Bool) -> Bool {
        guard let panel else { return false }
        let top = panel.frame.maxY
        var frame = panel.frameRect(forContentRect: NSRect(x: 0, y: 0, width: PanelView.width, height: contentHeight))
        frame.origin = NSPoint(x: panel.frame.minX, y: top - frame.height)
        guard frame != panel.frame else { return false }
        panel.setFrame(frame, display: display)
        return true
    }

    /// Under the menu bar, centred on the menu bar icon
    private func place(_ panel: NSPanel) {
        let anchor: NSPoint
        let screen: NSScreen?
        if let iconWindow = statusItem?.button?.window {
            anchor = NSPoint(x: iconWindow.frame.midX, y: iconWindow.frame.minY)
            screen = iconWindow.screen
        } else {
            anchor = NSEvent.mouseLocation
            screen = NSScreen.screens.first { NSMouseInRect(anchor, $0.frame, false) } ?? NSScreen.main
        }
        guard let area = screen?.visibleFrame else { return }
        var frame = panel.frame
        frame.origin.x = min(max(anchor.x - frame.width / 2, area.minX + 8), area.maxX - frame.width - 8)
        frame.origin.y = area.maxY - frame.height - 6
        panel.setFrame(frame, display: false)
    }
}

/// Holds the Touch ID glyph over the menu bar icon: borderless, above the
/// menu bar, and able to be key, which Touch ID needs, without activating
/// Orbit (the user keeps typing in the app in front)
private final class TouchIdOverlayPanel: NSPanel {
    init(over button: NSStatusBarButton, side: CGFloat) {
        var center = NSPoint(x: NSEvent.mouseLocation.x, y: NSEvent.mouseLocation.y)
        if let window = button.window {
            let frame = window.convertToScreen(button.convert(button.bounds, to: nil))
            center = NSPoint(x: frame.midX, y: frame.midY)
        }
        super.init(
            contentRect: NSRect(x: center.x - side / 2, y: center.y - side / 2, width: side, height: side),
            styleMask: [.borderless, .nonactivatingPanel],
            backing: .buffered,
            defer: false
        )
        isOpaque = false
        backgroundColor = .clear
        hasShadow = false
        level = .popUpMenu
        hidesOnDeactivate = false
        isReleasedWhenClosed = false
        collectionBehavior = [.canJoinAllSpaces, .stationary, .fullScreenAuxiliary, .ignoresCycle]
    }

    override var canBecomeKey: Bool { true }
}

/// The Touch ID fingerprint for the menu bar, the size of Orbit's logo: dim
/// while Touch ID waits; once it recognized the finger, each ridge is drawn
/// over in red from one end, the ridges starting one after another, like the
/// system's animation
private final class TouchIdSymbolView: NSView {
    /// The whole fill: the last ridge's start plus its drawing
    static let fillDuration: TimeInterval = 0.75

    private let side: CGFloat
    private var dimLayers: [CAShapeLayer] = []
    private var redLayers: [CAShapeLayer] = []

    /// Dim, like a menu bar item that is not active
    private static let waitingAlpha: CGFloat = 0.4
    /// Ridge width, in points: lighter than the logo's lines (1.5), the
    /// ridges being close together (wider ones merge at 18 pt)
    private static let lineWidth: CGFloat = 1

    private enum Segment {
        case move(CGPoint)
        case curve(CGPoint, CGPoint, CGPoint)
    }

    /// The ridges of the SF Symbols "touchid" glyph, traced as centre lines
    /// (y down, in `bounds`), each from the end where it starts filling
    private static let ridges: [[Segment]] = {
        typealias P = (CGFloat, CGFloat)
        func move(_ p: P) -> Segment { .move(CGPoint(x: p.0, y: p.1)) }
        func curve(_ a: P, _ b: P, _ c: P) -> Segment {
            .curve(CGPoint(x: a.0, y: a.1), CGPoint(x: b.0, y: b.1), CGPoint(x: c.0, y: c.1))
        }
        return [
        [move((165, 410)), curve((230, 250), (370, 158), (515, 158)), curve((590, 158), (640, 175), (683, 198))],
        [move((748, 240)), curve((815, 300), (870, 385), (885, 540))],
        [move((153, 490)), curve((150, 535), (155, 580), (165, 620))],
        [move((228, 728)), curve((265, 690), (270, 640), (255, 600)), curve((240, 550), (230, 520), (240, 460)), curve((260, 330), (380, 245), (515, 245)), curve((680, 245), (790, 360), (805, 600)), curve((810, 680), (800, 730), (780, 780))],
        [move((350, 625)), curve((320, 555), (320, 500), (335, 450)), curve((365, 380), (430, 335), (515, 335)), curve((555, 335), (580, 345), (600, 355))],
        [move((662, 400)), curve((700, 455), (718, 530), (718, 620)), curve((718, 720), (690, 800), (650, 855))],
        [move((355, 840)), curve((410, 795), (440, 730), (438, 650)), curve((437, 590), (418, 560), (418, 510)), curve((418, 460), (460, 425), (520, 425)), curve((600, 425), (625, 520), (625, 620)), curve((625, 720), (590, 820), (530, 880))],
        [move((517, 530)), curve((535, 585), (540, 640), (528, 690))],
        [move((348, 705)), curve((340, 735), (320, 765), (285, 790))],
        [move((510, 760)), curve((495, 800), (475, 840), (440, 870))],
        ]
    }()
    /// The traced box: the ridges' extent plus half a ridge
    private static let traced = CGRect(x: 133, y: 137, width: 772, height: 763)

    /// When each ridge starts filling, and how long it takes (seconds),
    /// scattered as in the system's animation
    private static let timing: [(start: Double, duration: Double)] = [
        (0.10, 0.50), (0.25, 0.35), (0.00, 0.30), (0.05, 0.70),
        (0.15, 0.45), (0.30, 0.40), (0.20, 0.55), (0.00, 0.35),
        (0.35, 0.30), (0.40, 0.30),
    ]

    init(side: CGFloat) {
        self.side = side
        super.init(frame: NSRect(x: 0, y: 0, width: side, height: side))
        wantsLayer = true
        let scale = side / Self.traced.width
        // traced coordinates (y down) to the layer's (y up), centred
        var transform = CGAffineTransform(
            a: scale, b: 0, c: 0, d: -scale,
            tx: -Self.traced.minX * scale,
            ty: side / 2 + Self.traced.midY * scale
        )
        for ridge in Self.ridges {
            let path = CGMutablePath()
            for segment in ridge {
                switch segment {
                case .move(let p): path.move(to: p)
                case .curve(let a, let b, let c): path.addCurve(to: c, control1: a, control2: b)
                }
            }
            let placed = path.copy(using: &transform)
            for red in [false, true] {
                let shape = CAShapeLayer()
                shape.frame = CGRect(x: 0, y: 0, width: side, height: side)
                shape.path = placed
                shape.fillColor = nil
                shape.lineWidth = Self.lineWidth
                shape.lineCap = .round
                shape.contentsScale = 2
                if red {
                    shape.strokeEnd = 0
                    redLayers.append(shape)
                } else {
                    dimLayers.append(shape)
                }
                layer?.addSublayer(shape)
            }
        }
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

    private func updateColors() {
        effectiveAppearance.performAsCurrentDrawingAppearance {
            let dim = NSColor.labelColor.withAlphaComponent(Self.waitingAlpha).cgColor
            let red = NSColor.systemRed.cgColor
            for shape in dimLayers {
                shape.strokeColor = dim
            }
            for shape in redLayers {
                shape.strokeColor = red
            }
        }
    }

    /// Draw every ridge over in red, over `fillDuration`
    func fill() {
        let now = CACurrentMediaTime()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        for (index, shape) in redLayers.enumerated() {
            let timing = Self.timing[index % Self.timing.count]
            let animation = CABasicAnimation(keyPath: "strokeEnd")
            animation.fromValue = 0
            animation.toValue = 1
            animation.beginTime = now + timing.start
            animation.duration = timing.duration
            animation.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            // empty until it starts
            animation.fillMode = .backwards
            shape.add(animation, forKey: "fill")
            shape.strokeEnd = 1
        }
        CATransaction.commit()
    }
}
