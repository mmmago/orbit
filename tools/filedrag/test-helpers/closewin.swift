// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: MIT
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import Cocoa
let names = Set(CommandLine.arguments.dropFirst())
let f = NSRunningApplication.runningApplications(withBundleIdentifier: "com.apple.finder").first!
let ax = AXUIElementCreateApplication(f.processIdentifier)
var v: CFTypeRef?; AXUIElementCopyAttributeValue(ax, kAXWindowsAttribute as CFString, &v)
for w in (v as? [AXUIElement]) ?? [] {
  var t: CFTypeRef?; AXUIElementCopyAttributeValue(w, kAXTitleAttribute as CFString, &t)
  guard let title = t as? String, names.contains(title) else { continue }
  var b: CFTypeRef?; AXUIElementCopyAttributeValue(w, kAXCloseButtonAttribute as CFString, &b)
  if let b { AXUIElementPerformAction(b as! AXUIElement, kAXPressAction as CFString); print("closed", title) }
}
