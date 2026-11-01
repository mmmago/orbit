// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: MIT

import Cocoa
// prints frontmost app changes with wall-clock times for N seconds
let f = DateFormatter(); f.dateFormat = "HH:mm:ss.SSS"
var last = ""
let end = Date().addingTimeInterval(Double(CommandLine.arguments[1])!)
while Date() < end {
  let a = NSWorkspace.shared.frontmostApplication
  let s = "\(a?.localizedName ?? "?") \(a?.processIdentifier ?? 0)"
  if s != last { print(f.string(from: Date()), s); fflush(stdout); last = s }
  Thread.sleep(forTimeInterval: 0.02)
}
