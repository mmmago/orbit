// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: MIT

// Test drop target: a window that logs every NSDraggingDestination callback and
// accepts file URLs and file promises (writing promises into a folder).
// usage: droptarget x,y,w,h <dest-folder> [seconds]
import Cocoa

let t0 = Date()
func log(_ s: String) { print(String(format: "[target %6.3f] ", Date().timeIntervalSince(t0)) + s); fflush(stdout) }

let r = CommandLine.arguments[1].split(separator: ",").compactMap { Double($0) }
let dest = URL(fileURLWithPath: CommandLine.arguments[2])
let lifetime = CommandLine.arguments.count > 3 ? Double(CommandLine.arguments[3])! : 15

final class DropView: NSView {
  var updates = 0
  override init(frame: NSRect) {
    super.init(frame: frame)
    registerForDraggedTypes(NSFilePromiseReceiver.readableDraggedTypes.map { NSPasteboard.PasteboardType($0) } + [.fileURL])
  }
  required init?(coder: NSCoder) { fatalError() }
  override func draw(_ dirtyRect: NSRect) { NSColor.systemTeal.withAlphaComponent(0.5).setFill(); dirtyRect.fill() }
  override func draggingEntered(_ s: NSDraggingInfo) -> NSDragOperation {
    log("draggingEntered types=\(s.draggingPasteboard.types?.map(\.rawValue) ?? []) mask=\(s.draggingSourceOperationMask.rawValue)")
    return .copy
  }
  override func draggingUpdated(_ s: NSDraggingInfo) -> NSDragOperation { updates += 1; return .copy }
  override func draggingExited(_ s: NSDraggingInfo?) { log("draggingExited after \(updates) updates") }
  override func prepareForDragOperation(_ s: NSDraggingInfo) -> Bool { log("prepareForDragOperation"); return true }
  override func performDragOperation(_ s: NSDraggingInfo) -> Bool {
    log("performDragOperation (updates=\(updates))")
    let pb = s.draggingPasteboard
    if let urls = pb.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL], !urls.isEmpty {
      log("received file URLs: \(urls.map(\.lastPathComponent))")
      return true
    }
    if let promises = pb.readObjects(forClasses: [NSFilePromiseReceiver.self]) as? [NSFilePromiseReceiver] {
      for p in promises {
        log("receiving promise types=\(p.fileTypes)")
        p.receivePromisedFiles(atDestination: dest, options: [:], operationQueue: OperationQueue()) { url, err in
          log("promise landed: \(url.lastPathComponent) err=\(String(describing: err))")
        }
      }
      return true
    }
    return false
  }
}

let app = NSApplication.shared
app.setActivationPolicy(.accessory)
let h = NSScreen.screens[0].frame.height
let w = NSWindow(contentRect: NSRect(x: r[0], y: h - r[1] - r[3], width: r[2], height: r[3]),
                 styleMask: [.titled], backing: .buffered, defer: false)
w.title = "droptarget"
w.contentView = DropView(frame: NSRect(x: 0, y: 0, width: r[2], height: r[3]))
w.level = .floating
w.orderFrontRegardless()
log("ready")
DispatchQueue.main.asyncAfter(deadline: .now() + lifetime) { log("exit"); exit(0) }
app.run()
