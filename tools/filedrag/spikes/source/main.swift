// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: MIT
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

// Spike B — source side.
// Simulates what the Deskflow server must do when the user drags a file in
// Finder towards the screen edge: detect that a *file* drag is in progress
// (drag pasteboard changeCount moved since mouse-down and holds file URLs),
// then cancel the local drag before handing it over to the other computer.
//
// After the cancel, the injected button is released over a separate logging
// drop target; if that target receives a drop, the cancel failed.
//
// usage: source --file x,y --edge x,y --land x,y --cancel none|esc|sink
import Cocoa

var filePt = CGPoint.zero, edge = CGPoint(x: 1300, y: 300), land = CGPoint(x: 1200, y: 700)
var cancel = "esc"
var it = CommandLine.arguments.dropFirst().makeIterator()
while let a = it.next() {
  switch a {
  case "--file": filePt = parsePoint(it.next()!)
  case "--edge": edge = parsePoint(it.next()!)
  case "--land": land = parsePoint(it.next()!)
  case "--cancel": cancel = it.next()!
  default: break
  }
}

let dragPB = NSPasteboard(name: .drag)

/// What Deskflow would call when the cursor hits the edge with a button held.
func draggedFiles(since snapshot: Int) -> [URL]? {
  guard dragPB.changeCount != snapshot else { return nil }
  guard dragPB.types?.contains(.fileURL) == true else { return nil }
  return dragPB.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL]
}

/// Invisible window that refuses every drop. Releasing over it ends the drag
/// with "no operation", which is a clean cancel. It also sees the drag.
final class SinkView: NSView {
  var entered = false
  override init(frame: NSRect) { super.init(frame: frame); registerForDraggedTypes([.fileURL]) }
  required init?(coder: NSCoder) { fatalError() }
  override func draw(_ r: NSRect) { NSColor(white: 0.5, alpha: 0.02).setFill(); r.fill() }
  override func draggingEntered(_ s: NSDraggingInfo) -> NSDragOperation {
    entered = true
    let n = s.draggingPasteboard.readObjects(forClasses: [NSURL.self], options: nil)?.count ?? 0
    log("sink: draggingEntered (sees \(n) item(s)) -> refusing")
    return []
  }
  override func draggingUpdated(_ s: NSDraggingInfo) -> NSDragOperation { [] }
  override func performDragOperation(_ s: NSDraggingInfo) -> Bool { log("sink: performDragOperation?!"); return false }
}

let app = NSApplication.shared
app.setActivationPolicy(.accessory)
var sinkWindow: NSWindow?
if cancel == "sink" {
  let c = cgToCocoa(edge), s: CGFloat = 120
  let w = NSWindow(contentRect: NSRect(x: c.x - s / 2, y: c.y - s / 2, width: s, height: s),
                   styleMask: .borderless, backing: .buffered, defer: false)
  w.isOpaque = false; w.backgroundColor = .clear; w.level = .popUpMenu; w.hasShadow = false
  w.contentView = SinkView(frame: NSRect(x: 0, y: 0, width: s, height: s))
  sinkWindow = w
}

let inj = Injector()
func onMain<T>(_ f: () -> T) -> T { DispatchQueue.main.sync(execute: f) }

DispatchQueue.global().asyncAfter(deadline: .now() + 0.4) {
  // 1. mouse-down on the file in Finder: Deskflow snapshots changeCount here.
  inj.move(to: filePt)
  Thread.sleep(forTimeInterval: 0.15)
  let snapshot = dragPB.changeCount
  log("mouseDown on file at \(filePt); drag pasteboard changeCount=\(snapshot)")
  inj.button(down: true)
  Thread.sleep(forTimeInterval: 0.25)

  // 2. drag towards the "edge", polling like Deskflow would on each move.
  let dragStart = Date()
  var detectedAt: TimeInterval?
  let start = inj.cursor
  for i in 1...40 {
    let f = CGFloat(i) / 40
    inj.move(to: CGPoint(x: start.x + (edge.x - start.x) * f, y: start.y + (edge.y - start.y) * f))
    Thread.sleep(forTimeInterval: 0.015)
    if detectedAt == nil, let urls = draggedFiles(since: snapshot) {
      detectedAt = Date().timeIntervalSince(dragStart)
      log(String(format: "FILE DRAG DETECTED after %.0f ms (step %d): %@", detectedAt! * 1000, i,
                 urls.map(\.lastPathComponent).joined(separator: ", ")))
    }
  }
  if detectedAt == nil { log("no file drag detected (changeCount=\(dragPB.changeCount))") }
  inj.hover(around: edge, count: 6)

  // 3. cancel the local drag (Deskflow would do this right before switchScreen).
  switch cancel {
  case "esc":
    log("cancel: posting Escape")
    inj.key(53)
  case "sink":
    if onMain({ (sinkWindow?.contentView as? SinkView)?.entered }) != true {
      log("sink was not entered (it is shown before the drag reaches it, see below)")
    }
    log("cancel: releasing button over sink window")
    inj.button(down: false)
  default:
    log("cancel: none (control run)")
  }
  Thread.sleep(forTimeInterval: 0.4)

  // 4. the "remote" phase: keep moving and release over the logging target.
  //    If the local drag is still alive, it will drop there.
  inj.glide(to: land, steps: 30)
  inj.hover(around: land, count: 10)
  if inj.buttonDown { inj.button(down: false) }
  log("released at landing target")
  Thread.sleep(forTimeInterval: 0.8)
  exit(0)
}
// Sink must exist under the edge before the drag arrives there.
sinkWindow?.orderFrontRegardless()
app.run()
