/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ServerProxy1_9.h"

#include "base/Log.h"
#include "client/Client.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

#include <cstring>

ServerProxy1_9::ServerProxy1_9(Client *client, deskflow::IStream *stream, IEventQueue *events)
    : ServerProxy1_8(client, stream, events),
      m_sender(events, stream, deskflow::filedrag::kClientTransferIdBase)
{
}

ServerProxy::ConnectionResult ServerProxy1_9::parseMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgDFileDragData, 4) == 0) {
    uint32_t id = 0;
    uint32_t index = 0;
    std::string data;
    if (ProtocolUtil::readf(getStream(), kMsgDFileDragData + 4, &id, &index, &data)) {
      getClient()->fileDragData(id, index, data);
    }
  } else if (memcmp(code, kMsgDFileDragAck, 4) == 0) {
    uint32_t id = 0;
    uint32_t chunks = 0;
    if (ProtocolUtil::readf(getStream(), kMsgDFileDragAck + 4, &id, &chunks)) {
      m_sender.ack(id, chunks);
    }
  } else if (memcmp(code, kMsgDFileDragFileEnd, 4) == 0) {
    uint32_t id = 0;
    uint32_t index = 0;
    if (ProtocolUtil::readf(getStream(), kMsgDFileDragFileEnd + 4, &id, &index)) {
      getClient()->fileDragFileEnd(id, index);
    }
  } else if (memcmp(code, kMsgDFileDragBegin, 4) == 0) {
    uint32_t id = 0;
    std::string manifest;
    if (ProtocolUtil::readf(getStream(), kMsgDFileDragBegin + 4, &id, &manifest)) {
      getClient()->fileDragBegin(id, manifest);
    }
  } else if (memcmp(code, kMsgDFileDragCancel, 4) == 0) {
    uint32_t id = 0;
    uint8_t reason = 0;
    if (ProtocolUtil::readf(getStream(), kMsgDFileDragCancel + 4, &id, &reason)) {
      // ids from the two directions never overlap (see FileDragSender)
      if (m_sender.owns(id)) {
        m_sender.cancelled(id, reason);
      } else {
        getClient()->fileDragCancelled(id, reason);
      }
    }
  } else if (memcmp(code, kMsgCFileDragHandOver, 4) == 0) {
    getClient()->fileDragHandOver();
  } else {
    return ServerProxy1_8::parseMessage(code);
  }
  return ConnectionResult::Okay;
}

void ServerProxy1_9::sendFileDragAck(uint32_t transferId, uint32_t chunksReceived)
{
  ProtocolUtil::writef(getStream(), kMsgDFileDragAck, transferId, chunksReceived);
}

void ServerProxy1_9::sendFileDragCancel(uint32_t transferId, uint8_t reason)
{
  LOG_DEBUG("file drag: telling server to stop transfer %u (reason %u)", transferId, reason);
  ProtocolUtil::writef(getStream(), kMsgDFileDragCancel, transferId, reason);
}

void ServerProxy1_9::sendFileDragState(bool dragging)
{
  LOG_DEBUG("file drag: telling server files are %sbeing dragged here", dragging ? "" : "no longer ");
  ProtocolUtil::writef(getStream(), kMsgDFileDragState, static_cast<uint8_t>(dragging ? 1 : 0));
}

bool ServerProxy1_9::startFileDrag(const std::vector<std::string> &paths)
{
  return m_sender.start(paths);
}
