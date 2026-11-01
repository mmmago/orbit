// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: MIT

import Cocoa
// Drive deskflow-drag-helper like OSXComputer does on a server:
// begin at the cursor, wait for "ready", press at the cursor (tagged), let a
// simulated user drag to --drop and release. Items are prepared staging files.
// usage: helperharness <helper> --drop x,y [--moves-before-ready] file...
var helper = "", drop = CGPoint(x: 940, y: 400), files: [String] = [], movesEarly = false, untagged = false, withNumber = false
var it = CommandLine.arguments.dropFirst().makeIterator(); helper = it.next()!
while let a = it.next() {
  switch a { case "--drop": let p = it.next()!.split(separator: ",").map { Double($0)! }; drop = CGPoint(x: p[0], y: p[1])
  case "--moves-before-ready": movesEarly = true
  case "--untagged": untagged = true
  case "--event-number": withNumber = true
  default: files.append(a) }
}
let t0 = Date(); func log(_ s: String) { print(String(format: "[harness %6.3f] ", Date().timeIntervalSince(t0)) + s); fflush(stdout) }
let p = Process(); p.executableURL = URL(fileURLWithPath: helper)
let inPipe = Pipe(), outPipe = Pipe(); p.standardInput = inPipe; p.standardOutput = outPipe
try! p.run()
func send(_ o: [String: Any]) { var d = try! JSONSerialization.data(withJSONObject: o); d.append(10); inPipe.fileHandleForWriting.write(d) }
let cur = CGEvent(source: nil)!.location
var items: [[String: Any]] = []
for f in files {
  var isDir: ObjCBool = false; FileManager.default.fileExists(atPath: f, isDirectory: &isDir)
  items.append(["name": (f as NSString).lastPathComponent, "size": 1, "path": f, "isDir": isDir.boolValue, "count": 1])
}
var ready = false, placed = false, ended = false, delivered = 0
let reader = outPipe.fileHandleForReading
reader.readabilityHandler = { h in
  for line in String(data: h.availableData, encoding: .utf8)!.split(separator: "\n") {
    log("helper: \(line)")
    if line.contains("\"ready\"") { ready = true }
    if line.contains("\"placed\"") { placed = true }
    if line.contains("\"ended\"") { ended = true }
    if line.contains("\"delivered\"") { delivered += 1 }
  }
}
Thread.sleep(forTimeInterval: 0.5)
log("begin at cursor \(cur)")
send(["cmd": "begin", "id": 1, "x": cur.x, "y": cur.y, "items": items])
for i in 0..<items.count { send(["cmd": "itemReady", "id": 1, "index": i]) }
func move(_ to: CGPoint, dragged: Bool) {
  let e = CGEvent(mouseEventSource: nil, mouseType: dragged ? .leftMouseDragged : .mouseMoved, mouseCursorPosition: to, mouseButton: .left)!
  e.post(tap: .cghidEventTap)
}
var pos = cur
if movesEarly { // the user keeps moving while the drag is being set up
  for i in 1...6 { pos = CGPoint(x: cur.x - CGFloat(i * 4), y: cur.y + CGFloat(i * 2)); move(pos, dragged: true); Thread.sleep(forTimeInterval: 0.012) }
}
let deadline = Date().addingTimeInterval(2); while !ready && Date() < deadline { Thread.sleep(forTimeInterval: 0.005) }
log("ready=\(ready)")
let now = CGEvent(source: nil)!.location
send(["cmd": "pressAt", "id": 1, "x": now.x, "y": now.y])
let d1 = Date().addingTimeInterval(1); while !placed && Date() < d1 { Thread.sleep(forTimeInterval: 0.002) }
log("placed=\(placed)")
let down = CGEvent(mouseEventSource: nil, mouseType: .leftMouseDown, mouseCursorPosition: now, mouseButton: .left)!
down.setIntegerValueField(.mouseEventClickState, value: 1)
if !untagged { down.setIntegerValueField(.eventSourceUserData, value: 0x44464447) }
var eventNumber: Int64 = 0
if withNumber {
  eventNumber = Int64(CGEventSource.counterForEventType(.hidSystemState, eventType: .leftMouseDown) + CGEventSource.counterForEventType(.hidSystemState, eventType: .rightMouseDown) + CGEventSource.counterForEventType(.hidSystemState, eventType: .otherMouseDown)) + 1
  down.setIntegerValueField(.mouseEventNumber, value: eventNumber)
}
down.post(tap: .cghidEventTap)
log("pressed at \(now)")
Thread.sleep(forTimeInterval: 0.1)
let start = now
for i in 1...40 { move(CGPoint(x: start.x + (drop.x - start.x) * CGFloat(i) / 40, y: start.y + (drop.y - start.y) * CGFloat(i) / 40), dragged: true); Thread.sleep(forTimeInterval: 0.015) }
for i in 0..<15 { move(CGPoint(x: drop.x + (i % 2 == 0 ? 1 : -1), y: drop.y), dragged: true); Thread.sleep(forTimeInterval: 0.05) }
CGEvent(mouseEventSource: nil, mouseType: .leftMouseUp, mouseCursorPosition: drop, mouseButton: .left)!.post(tap: .cghidEventTap)
log("released at \(drop)")
let d2 = Date().addingTimeInterval(5); while (!ended || delivered < items.count) && Date() < d2 { Thread.sleep(forTimeInterval: 0.05) }
log("ended=\(ended) delivered=\(delivered)/\(items.count)")
inPipe.fileHandleForWriting.closeFile(); p.waitUntilExit()
