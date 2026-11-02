/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include "client/ServerProxy1_9.h"

//! Proxy for server implementing protocol version 1.10 (trackpad gestures) and later
class ServerProxy1_10 : public ServerProxy1_9
{
public:
  ServerProxy1_10(
      Client *client, deskflow::IStream *stream, IEventQueue *events, bool largeManifests = false,
      bool screenUnlock = false
  );
  ~ServerProxy1_10() override = default;

  //! True if the server can unlock this client with Touch ID (1.13+)
  bool supportsScreenUnlock() const
  {
    return m_screenUnlock;
  }

  //! Report the lock state (deskflow::screenunlock::State) and its token
  void sendScreenLockState(uint8_t state, const std::string &token);

protected:
  ConnectionResult parseMessage(const uint8_t *code) override;

private:
  void trackpadEvent();
  void screenUnlock();

  bool m_screenUnlock; // negotiated protocol >= 1.13
};
