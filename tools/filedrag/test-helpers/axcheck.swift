// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: MIT
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import ApplicationServices
import Cocoa
print("AXIsProcessTrusted:", AXIsProcessTrusted())
print("CGPreflightPostEventAccess:", CGPreflightPostEventAccess())
print("CGPreflightListenEventAccess:", CGPreflightListenEventAccess())
for s in NSScreen.screens { print("screen", s.frame) }
print("mouse", NSEvent.mouseLocation)
