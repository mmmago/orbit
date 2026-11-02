/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#include "server/ClientProxy1_10.h"

#include "deskflow/ProtocolTypes.h"
#include "base/Log.h"
#include "deskflow/ProtocolUtil.h"
#include "server/Server.h"

#include <cstring>

ClientProxy1_10::ClientProxy1_10(
    const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events, bool largeManifests,
    bool fileDragReturn, bool screenUnlock
)
    : ClientProxy1_9(name, adoptedStream, server, events, largeManifests),
      m_fileDragReturn(fileDragReturn),
      m_screenUnlock(screenUnlock)
{
}

void ClientProxy1_10::returnFileDrag()
{
  if (m_fileDragReturn) {
    ProtocolUtil::writef(getStream(), kMsgCFileDragReturn);
  }
}

void ClientProxy1_10::trackpadEvent(const std::string &bytes)
{
  ProtocolUtil::writef(getStream(), kMsgDTrackpadEvent, &bytes);
}

void ClientProxy1_10::unlockScreen(const std::string &token)
{
  if (m_screenUnlock && !token.empty()) {
    ProtocolUtil::writef(getStream(), kMsgCScreenUnlock, &token);
  }
}

bool ClientProxy1_10::parseMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgDScreenLockState, 4) != 0) {
    return ClientProxy1_9::parseMessage(code);
  }
  uint8_t state = 0;
  std::string token;
  if (!ProtocolUtil::readf(getStream(), kMsgDScreenLockState + 4, &state, &token)) {
    return true;
  }
  LOG_INFO(
      "screen unlock: \"%s\" is %s", getName().c_str(),
      state == 0 ? "unlocked" : (state == 2 ? "locked, unlockable with Touch ID" : "locked")
  );
  m_screenUnlockToken = state == 2 ? token : std::string();
  getServer()->clientScreenLockChanged(this);
  return true;
}
