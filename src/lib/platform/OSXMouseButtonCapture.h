/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-02 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include "platform/OSXFileDrag.h"

#include <ApplicationServices/ApplicationServices.h>
#include <cstdint>

namespace deskflow::osx {

//! Primary event tap: a press delivered locally needs a local release even
//! after leaving. Otherwise macOS reports a held button and locks the cursor.
class MouseButtonCapture
{
public:
  bool passToSystem(CGEventType type, CGEventRef event, bool onComputer)
  {
    bool pass =
        onComputer || CGEventGetIntegerValueField(event, kCGEventSourceUserData) == filedrag::kSyntheticEventTag;
    switch (type) {
    case kCGEventLeftMouseDown:
    case kCGEventRightMouseDown:
    case kCGEventOtherMouseDown: {
      const auto button = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
      if (pass && button >= 0 && button < 32) {
        m_pressedLocally |= uint32_t(1) << button;
      }
      break;
    }
    case kCGEventLeftMouseUp:
    case kCGEventRightMouseUp:
    case kCGEventOtherMouseUp: {
      const auto button = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
      if (button >= 0 && button < 32) {
        const auto mask = uint32_t(1) << button;
        pass = pass || (m_pressedLocally & mask) != 0;
        m_pressedLocally &= ~mask;
      }
      break;
    }
    default:
      break;
    }
    return pass;
  }

private:
  uint32_t m_pressedLocally = 0; // event tap thread only
};

} // namespace deskflow::osx
