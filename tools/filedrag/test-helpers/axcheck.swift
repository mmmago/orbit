// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: MIT

import ApplicationServices
import Cocoa
print("AXIsProcessTrusted:", AXIsProcessTrusted())
print("CGPreflightPostEventAccess:", CGPreflightPostEventAccess())
print("CGPreflightListenEventAccess:", CGPreflightListenEventAccess())
for s in NSScreen.screens { print("screen", s.frame) }
print("mouse", NSEvent.mouseLocation)
