// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: MIT
// Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.

import Cocoa
import Carbon
print("GetCurrentButtonState=\(GetCurrentButtonState()) combined=\(CGEventSource.buttonState(.combinedSessionState, button: .left)) hid=\(CGEventSource.buttonState(.hidSystemState, button: .left)) NSEvent.pressedMouseButtons=\(NSEvent.pressedMouseButtons)")
