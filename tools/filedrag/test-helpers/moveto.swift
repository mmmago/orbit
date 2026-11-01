// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: MIT
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import CoreGraphics
let a = CommandLine.arguments; CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: CGPoint(x: Double(a[1])!, y: Double(a[2])!), mouseButton: .left)!.post(tap: .cghidEventTap)
