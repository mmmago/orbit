// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: MIT

import Cocoa
let l = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as! [[String: Any]]
for w in l where ((w[kCGWindowOwnerName as String] as? String) ?? "").lowercased().contains("deskflow") {
  let b = w[kCGWindowBounds as String] as! [String: Any]
  print(w[kCGWindowNumber as String]!, w[kCGWindowOwnerName as String]!, w[kCGWindowOwnerPID as String]!, "layer", w[kCGWindowLayer as String]!, "onscreen", w[kCGWindowIsOnscreen as String] ?? 0, b["X"]!, b["Y"]!, b["Width"]!, b["Height"]!)
}
