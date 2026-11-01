// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: MIT
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

// Synthetic mouse/keyboard injection that mirrors OSXComputer::postMouseEvent
// and OSXComputer::fakeMouseButton (event numbers on macOS 27+, click state,
// deltas), so spike results carry over to Deskflow's real injection path.
import Cocoa

let t0 = Date()
func log(_ s: String) {
  print(String(format: "[%6.3f] ", Date().timeIntervalSince(t0)) + s)
  fflush(stdout)
}

final class Injector {
  private(set) var cursor: CGPoint
  private(set) var buttonDown = false
  private var eventNumber: Int64 = 0
  private let needsEventNumber = ProcessInfo.processInfo.operatingSystemVersion.majorVersion >= 27

  init() {
    cursor = CGEvent(source: nil)?.location ?? .zero
  }

  func move(to p: CGPoint) {
    let type: CGEventType = buttonDown ? .leftMouseDragged : .mouseMoved
    guard let e = CGEvent(mouseEventSource: nil, mouseType: type, mouseCursorPosition: p, mouseButton: .left) else { return }
    if buttonDown && needsEventNumber {
      e.setIntegerValueField(.mouseEventNumber, value: eventNumber)
    }
    e.setIntegerValueField(.mouseEventClickState, value: 1)
    let dx = Int64(p.x - cursor.x), dy = Int64(p.y - cursor.y)
    e.setIntegerValueField(.mouseEventDeltaX, value: dx)
    e.setIntegerValueField(.mouseEventDeltaY, value: dy)
    e.setDoubleValueField(.mouseEventDeltaX, value: Double(dx))
    e.setDoubleValueField(.mouseEventDeltaY, value: Double(dy))
    e.post(tap: .cghidEventTap)
    cursor = p
  }

  func button(down: Bool) {
    guard let e = CGEvent(mouseEventSource: nil, mouseType: down ? .leftMouseDown : .leftMouseUp,
                          mouseCursorPosition: cursor, mouseButton: .left) else { return }
    if needsEventNumber {
      if down {
        if eventNumber == 0 {
          eventNumber = Int64(CGEventSource.counterForEventType(.hidSystemState, eventType: .leftMouseDown)
            + CGEventSource.counterForEventType(.hidSystemState, eventType: .rightMouseDown)
            + CGEventSource.counterForEventType(.hidSystemState, eventType: .otherMouseDown))
        }
        eventNumber += 1
      }
      e.setIntegerValueField(.mouseEventNumber, value: eventNumber)
    }
    e.setIntegerValueField(.mouseEventClickState, value: 1)
    buttonDown = down
    e.post(tap: .cghidEventTap)
  }

  func key(_ code: CGKeyCode) {
    CGEvent(keyboardEventSource: nil, virtualKey: code, keyDown: true)?.post(tap: .cghidEventTap)
    Thread.sleep(forTimeInterval: 0.02)
    CGEvent(keyboardEventSource: nil, virtualKey: code, keyDown: false)?.post(tap: .cghidEventTap)
  }

  /// Linear glide, posting one event per step (blocking; call off the main thread).
  func glide(to p: CGPoint, steps: Int = 30, interval: TimeInterval = 0.015) {
    let start = cursor
    for i in 1...max(1, steps) {
      let f = CGFloat(i) / CGFloat(steps)
      move(to: CGPoint(x: start.x + (p.x - start.x) * f, y: start.y + (p.y - start.y) * f))
      Thread.sleep(forTimeInterval: interval)
    }
  }

  /// Small jiggle so drop targets get repeated drag-over updates.
  func hover(around p: CGPoint, count: Int = 8, interval: TimeInterval = 0.05) {
    for i in 0..<count {
      move(to: CGPoint(x: p.x + CGFloat(i % 2 == 0 ? 1 : -1), y: p.y))
      Thread.sleep(forTimeInterval: interval)
    }
  }
}

func parsePoint(_ s: String) -> CGPoint {
  let parts = s.split(separator: ",").compactMap { Double($0) }
  return CGPoint(x: parts[0], y: parts[1])
}

/// CG (top-left origin of main display) -> Cocoa (bottom-left origin) coordinates.
func cgToCocoa(_ p: CGPoint) -> NSPoint {
  NSPoint(x: p.x, y: NSScreen.screens[0].frame.height - p.y)
}
