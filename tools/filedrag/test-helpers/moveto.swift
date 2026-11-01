// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: MIT

import CoreGraphics
let a = CommandLine.arguments; CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: CGPoint(x: Double(a[1])!, y: Double(a[2])!), mouseButton: .left)!.post(tap: .cghidEventTap)
