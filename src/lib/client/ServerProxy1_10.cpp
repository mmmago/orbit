/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#include "client/ServerProxy1_10.h"

#include "base/Log.h"
#include "client/Client.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

#include <cstring>

ServerProxy1_10::ServerProxy1_10(
    Client *client, deskflow::IStream *stream, IEventQueue *events, bool largeManifests, bool screenUnlock
)
    : ServerProxy1_9(client, stream, events, largeManifests),
      m_screenUnlock(screenUnlock)
{
}

ServerProxy::ConnectionResult ServerProxy1_10::parseMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgDTrackpadEvent, 4) == 0) {
    trackpadEvent();
    return ConnectionResult::Okay;
  }
  if (m_screenUnlock && memcmp(code, kMsgCScreenUnlock, 4) == 0) {
    screenUnlock();
    return ConnectionResult::Okay;
  }
  return ServerProxy1_9::parseMessage(code);
}

void ServerProxy1_10::trackpadEvent()
{
  std::string bytes;
  if (!ProtocolUtil::readf(getStream(), kMsgDTrackpadEvent + 4, &bytes)) {
    return;
  }
  LOG_VERBOSE("recv trackpad event, %zu bytes", bytes.size());
  getClient()->trackpadEvent(bytes);
}

void ServerProxy1_10::screenUnlock()
{
  std::string token;
  if (!ProtocolUtil::readf(getStream(), kMsgCScreenUnlock + 4, &token)) {
    return;
  }
  getClient()->unlockScreen(token);
}

void ServerProxy1_10::sendScreenLockState(uint8_t state, const std::string &token)
{
  if (m_screenUnlock) {
    ProtocolUtil::writef(getStream(), kMsgDScreenLockState, state, &token);
  }
}
