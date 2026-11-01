// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import AppKit
import SwiftUI

/// Shows the menu bar panel. Used from Objective-C++ (gui/OrbitPanel.mm):
/// Orbit sends the state as JSON with `update(_:)`, and gets the panel's
/// actions back through `onAction` as a name and JSON arguments.
@MainActor
@objc(OrbitPanelController)
public final class OrbitPanelController: NSObject {
    @objc public var onAction: ((String, String) -> Void)?

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
