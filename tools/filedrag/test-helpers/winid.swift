// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: MIT

import Cocoa
let l = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as! [[String: Any]]
for w in l where (w[kCGWindowOwnerName as String] as? String) == "Finder" && (w[kCGWindowName as String] as? String) == CommandLine.arguments[1] {
  print(w[kCGWindowNumber as String]!); break
}
