// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: MIT

// Spike A — receiver side.
// Simulates what the Deskflow client must do when a file drag crosses onto
// it: show a tiny transparent window under the cursor, inject a mouse-down
// into it, start a native drag session with NSFilePromiseProviders, then let
// injected drag events (as Deskflow's fakeMouseMove would send) carry it to
// a drop target and release. The promise is fulfilled only on drop, optionally
// after a delay to simulate data still streaming over the network.
//
// usage: receiver --from x,y --to x,y [--delay secs] [--keep-window] file...
import Cocoa
import UniformTypeIdentifiers

var files: [URL] = []
var from = CGPoint(x: 1100, y: 600)
var to = CGPoint(x: 500, y: 400)
var delay: TimeInterval = 0
var keepWindow = false
var useURL = false
var hoverCount = 8
var args = CommandLine.arguments.dropFirst().makeIterator()
while let a = args.next() {
  switch a {
  case "--from": from = parsePoint(args.next()!)
  case "--to": to = parsePoint(args.next()!)
  case "--delay": delay = Double(args.next()!)!
  case "--keep-window": keepWindow = true
  case "--url": useURL = true
  case "--hover": hoverCount = Int(args.next()!)!
  default: files.append(URL(fileURLWithPath: a))
  }
}

final class GrabView: NSView, NSDraggingSource, NSFilePromiseProviderDelegate {
  let writeQueue: OperationQueue = {
    let q = OperationQueue()
    q.qualityOfService = .userInitiated
    return q
  }()
  var moves = 0
  var promisesPending = 0
  var dragEnded = false
  var onDragStarted: (() -> Void)?

  override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

  override func draw(_ dirtyRect: NSRect) {
    // Not fully transparent: borderless windows let clicks through clear pixels.
    NSColor(white: 0.5, alpha: 0.02).setFill()
    dirtyRect.fill()
  }

  override func mouseDown(with event: NSEvent) {
    log("helper window got mouseDown (eventNumber=\(event.eventNumber), clickCount=\(event.clickCount))")
    let loc = convert(event.locationInWindow, from: nil)
    var items: [NSDraggingItem] = []
    for (i, f) in files.enumerated() {
      let type = UTType(filenameExtension: f.pathExtension)?.identifier ?? UTType.data.identifier
      let writer: NSPasteboardWriting
      if useURL {
        writer = f as NSURL
      } else {
        let provider = NSFilePromiseProvider(fileType: type, delegate: self)
        provider.userInfo = f
        writer = provider
      }
      let item = NSDraggingItem(pasteboardWriter: writer)
      let icon = NSWorkspace.shared.icon(forFile: f.path)
      icon.size = NSSize(width: 64, height: 64)
      item.setDraggingFrame(NSRect(x: loc.x - 32 + CGFloat(i * 6), y: loc.y - 32 - CGFloat(i * 6), width: 64, height: 64),
                            contents: icon)
      items.append(item)
    }
    promisesPending = useURL ? 0 : items.count
    let session = beginDraggingSession(with: items, event: event, source: self)
    session.animatesToStartingPositionsOnCancelOrFail = false
    session.draggingFormation = .pile
    log("beginDraggingSession started with \(items.count) \(useURL ? "file URL(s)" : "promise(s)")")
    onDragStarted?()
  }

  // MARK: NSDraggingSource
  func draggingSession(_ session: NSDraggingSession, sourceOperationMaskFor context: NSDraggingContext) -> NSDragOperation {
    context == .outsideApplication ? .copy : []
  }
  func draggingSession(_ session: NSDraggingSession, willBeginAt screenPoint: NSPoint) {
    log("drag session willBegin at \(screenPoint)")
  }
  func draggingSession(_ session: NSDraggingSession, movedTo screenPoint: NSPoint) { moves += 1 }
  func draggingSession(_ session: NSDraggingSession, endedAt screenPoint: NSPoint, operation: NSDragOperation) {
    log("drag session ended at \(screenPoint), operation=\(operation.rawValue) (\(operation.isEmpty ? "CANCELLED/REFUSED" : "ACCEPTED")), movedTo callbacks=\(moves)")
    dragEnded = true
    if operation.isEmpty { promisesPending = 0 }
  }

  // MARK: NSFilePromiseProviderDelegate
  func filePromiseProvider(_ p: NSFilePromiseProvider, fileNameForType fileType: String) -> String {
    (p.userInfo as! URL).lastPathComponent
  }
  func operationQueue(for p: NSFilePromiseProvider) -> OperationQueue { writeQueue }
  func filePromiseProvider(_ p: NSFilePromiseProvider, writePromiseTo url: URL, completionHandler: @escaping (Error?) -> Void) {
    let src = p.userInfo as! URL
    log("promise write requested: \(url.path)")
    if delay > 0 {
      log("simulating \(delay)s of network transfer...")
      Thread.sleep(forTimeInterval: delay)
    }
    do {
      try FileManager.default.copyItem(at: src, to: url)
      log("promise fulfilled: \(url.lastPathComponent)")
      completionHandler(nil)
    } catch {
      log("promise FAILED: \(error)")
      completionHandler(error)
    }
    DispatchQueue.main.async { self.promisesPending -= 1 }
  }
}

let app = NSApplication.shared
app.setActivationPolicy(.accessory)

let size: CGFloat = 48
let c = cgToCocoa(from)
let window = NSWindow(contentRect: NSRect(x: c.x - size / 2, y: c.y - size / 2, width: size, height: size),
                      styleMask: .borderless, backing: .buffered, defer: false)
window.level = .popUpMenu
window.isOpaque = false
window.backgroundColor = .clear
window.hasShadow = false
window.ignoresMouseEvents = false
let view = GrabView(frame: NSRect(x: 0, y: 0, width: size, height: size))
window.contentView = view
view.onDragStarted = {
  if !keepWindow {
    window.orderOut(nil)
    log("helper window hidden")
  }
}
window.orderFrontRegardless()
log("helper window shown at CG \(from)")

let inj = Injector()
DispatchQueue.global().asyncAfter(deadline: .now() + 0.4) {
  inj.move(to: from)
  Thread.sleep(forTimeInterval: 0.1)
  log("inject mouseDown at \(from)")
  inj.button(down: true)
  Thread.sleep(forTimeInterval: 0.15)
  inj.glide(to: CGPoint(x: from.x - 6, y: from.y + 4), steps: 3)
  log("inject drag to \(to)")
  inj.glide(to: to, steps: 40)
  inj.hover(around: to, count: hoverCount)
  log("inject mouseUp")
  inj.button(down: false)
}

// Exit once the session ended and all promises are written (or on timeout).
let deadline = Date().addingTimeInterval(20 + delay)
Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { _ in
  if (view.dragEnded && view.promisesPending <= 0) || Date() > deadline {
    if Date() > deadline { log("TIMEOUT dragEnded=\(view.dragEnded) pending=\(view.promisesPending)") }
    log("done")
    exit(0)
  }
}
app.run()
