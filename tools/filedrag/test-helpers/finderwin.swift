// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: MIT
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

// Test harness helper: open a folder in Finder, move/resize its window via
// Accessibility to a known spot on the main display, print its CG center.
// usage: finderwin <folder> x,y,w,h
import Cocoa

let folder = URL(fileURLWithPath: CommandLine.arguments[1])
let r = CommandLine.arguments[2].split(separator: ",").compactMap { Double($0) }
NSWorkspace.shared.open(folder)

guard let finder = NSRunningApplication.runningApplications(withBundleIdentifier: "com.apple.finder").first else {
  print("no Finder"); exit(1)
}
let ax = AXUIElementCreateApplication(finder.processIdentifier)
for _ in 0..<40 {
  var value: CFTypeRef?
  AXUIElementCopyAttributeValue(ax, kAXWindowsAttribute as CFString, &value)
  for w in (value as? [AXUIElement]) ?? [] {
    var title: CFTypeRef?
    AXUIElementCopyAttributeValue(w, kAXTitleAttribute as CFString, &title)
    if (title as? String) == folder.lastPathComponent {
      var pos = CGPoint(x: r[0], y: r[1]), size = CGSize(width: r[2], height: r[3])
      AXUIElementSetAttributeValue(w, kAXPositionAttribute as CFString, AXValueCreate(.cgPoint, &pos)!)
      AXUIElementSetAttributeValue(w, kAXSizeAttribute as CFString, AXValueCreate(.cgSize, &size)!)
      AXUIElementPerformAction(w, kAXRaiseAction as CFString)
      print("\(Int(r[0] + r[2] / 2)),\(Int(r[1] + r[3] / 2 + 40))")
      exit(0)
    }
  }
  Thread.sleep(forTimeInterval: 0.1)
}
print("window not found"); exit(1)
