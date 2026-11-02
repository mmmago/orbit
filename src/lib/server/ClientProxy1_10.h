/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include "server/ClientProxy1_9.h"

//! Proxy for client implementing protocol version 1.10 (trackpad gestures) and later
class ClientProxy1_10 : public ClientProxy1_9
{
public:
  ClientProxy1_10(
      const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events,
      bool largeManifests = false, bool fileDragReturn = false, bool screenUnlock = false
  );
  ~ClientProxy1_10() override = default;

  bool supportsFileDragReturn() const override
  {
    return m_fileDragReturn;
  }
  void returnFileDrag() override;

  void trackpadEvent(const std::string &bytes) override;
  bool supportsTrackpadEvents() const override
  {
    return true;
  }

  std::string screenUnlockToken() const override
  {
    return m_screenUnlockToken;
  }
  void unlockScreen(const std::string &token) override;

protected:
  bool parseMessage(const uint8_t *code) override;

private:
  bool m_fileDragReturn; // negotiated protocol >= 1.12
  bool m_screenUnlock;   // negotiated protocol >= 1.13
  // set while the client is locked and can be unlocked from here
  std::string m_screenUnlockToken;
};
