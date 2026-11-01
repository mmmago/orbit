// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: MIT

// Test harness helper: find an item by name in the frontmost Finder window
// via Accessibility and print its CG center ("x,y").
// usage: findrow <name>
import Cocoa

let name = CommandLine.arguments[1]
let finder = NSRunningApplication.runningApplications(withBundleIdentifier: "com.apple.finder").first!
let app = AXUIElementCreateApplication(finder.processIdentifier)

func attr(_ e: AXUIElement, _ a: String) -> CFTypeRef? {
  var v: CFTypeRef?
  AXUIElementCopyAttributeValue(e, a as CFString, &v)
  return v
}
func frame(_ e: AXUIElement) -> CGRect? {
  guard let p = attr(e, kAXPositionAttribute), let s = attr(e, kAXSizeAttribute) else { return nil }
  var pt = CGPoint.zero, sz = CGSize.zero
  AXValueGetValue(p as! AXValue, .cgPoint, &pt)
  AXValueGetValue(s as! AXValue, .cgSize, &sz)
  return CGRect(origin: pt, size: sz)
}
func search(_ e: AXUIElement, depth: Int) -> CGRect? {
  if depth > 14 { return nil }
  for key in [kAXValueAttribute, kAXTitleAttribute, kAXDescriptionAttribute] {
    if (attr(e, key) as? String) == name, let f = frame(e), f.width > 0 { return f }
  }
  for c in (attr(e, kAXChildrenAttribute) as? [AXUIElement]) ?? [] {
    if let f = search(c, depth: depth + 1) { return f }
  }
  return nil
}
guard let win = (attr(app, kAXWindowsAttribute) as? [AXUIElement])?.first, let f = search(win, depth: 0) else {
  print("not found"); exit(1)
}
print("\(Int(f.midX)),\(Int(f.midY))")
