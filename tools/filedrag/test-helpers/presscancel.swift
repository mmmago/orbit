// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: MIT

import Cocoa
// Press the button with tooltip "Arrêter"/"Stop" in the windows of process argv[1].
let pid = pid_t(CommandLine.arguments[1])!
let wanted = CommandLine.arguments.count > 2 ? Int(CommandLine.arguments[2])! : 0
let app = AXUIElementCreateApplication(pid)
func attr(_ e: AXUIElement, _ a: String) -> CFTypeRef? { var v: CFTypeRef?; AXUIElementCopyAttributeValue(e, a as CFString, &v); return v }
var found: [(CGFloat, AXUIElement)] = []
func collect(_ e: AXUIElement, _ d: Int) {
  if (attr(e, kAXRoleAttribute) as? String) == "AXButton", let h = attr(e, kAXHelpAttribute) as? String, h == "Arrêter" || h == "Stop" {
    var pt = CGPoint.zero; if let v = attr(e, kAXPositionAttribute) { AXValueGetValue(v as! AXValue, .cgPoint, &pt) }
    found.append((pt.y, e))
  }
  if d > 6 { return }
  for c in (attr(e, kAXChildrenAttribute) as? [AXUIElement]) ?? [] { collect(c, d + 1) }
}
for w in (attr(app, kAXWindowsAttribute) as? [AXUIElement]) ?? [] { collect(w, 0) }
found.sort { $0.0 < $1.0 } // top row first
if wanted < found.count { AXUIElementPerformAction(found[wanted].1, kAXPressAction as CFString); print("pressed cancel of row \(wanted + 1) of \(found.count)"); exit(0) }
print("cancel button not found")
