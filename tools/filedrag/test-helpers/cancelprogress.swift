// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: MIT

import Foundation
// Subscribe to file progress published for argv[1] (like Finder) and cancel it once seen.
let url = URL(fileURLWithPath: CommandLine.arguments[1])
var done = false
let token = Progress.addSubscriber(forFileURL: url) { progress in
  print(String(format: "seen progress %.0f%%, cancelling", progress.fractionCompleted * 100)); fflush(stdout)
  progress.cancel()
  done = true
  return nil
}
let deadline = Date().addingTimeInterval(20)
while !done && Date() < deadline { RunLoop.current.run(until: Date().addingTimeInterval(0.1)) }
Progress.removeSubscriber(token)
if !done { print("no progress seen") }
