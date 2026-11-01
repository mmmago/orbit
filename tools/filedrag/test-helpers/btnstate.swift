// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: MIT

import Cocoa
import Carbon
print("GetCurrentButtonState=\(GetCurrentButtonState()) combined=\(CGEventSource.buttonState(.combinedSessionState, button: .left)) hid=\(CGEventSource.buttonState(.hidSystemState, button: .left)) NSEvent.pressedMouseButtons=\(NSEvent.pressedMouseButtons)")
