/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_9.h"

#include "base/Log.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "server/Server.h"

#include <cstring>

using namespace deskflow::filedrag;

ClientProxy1_9::ClientProxy1_9(
    const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events
)
    : ClientProxy1_8(name, adoptedStream, server, events),
      m_sender(events, getStream(), kServerTransferIdBase)
{
}

bool ClientProxy1_9::startFileDrag(const std::vector<std::string> &paths)
{
  return m_sender.start(paths);
}

void ClientProxy1_9::requestFileDragHandOver()
{
  LOG_DEBUG("file drag: asking \"%s\" to hand over its drag", getName().c_str());
  m_clientDragging = false;
  ProtocolUtil::writef(getStream(), kMsgCFileDragHandOver);
}

void ClientProxy1_9::sendFileDragAck(uint32_t transferId, uint32_t chunksReceived)
{
  ProtocolUtil::writef(getStream(), kMsgDFileDragAck, transferId, chunksReceived);
}

void ClientProxy1_9::sendFileDragCancel(uint32_t transferId, uint8_t reason)
{
  ProtocolUtil::writef(getStream(), kMsgDFileDragCancel, transferId, reason);
}

bool ClientProxy1_9::parseMessage(const uint8_t *code)
{
  auto *stream = getStream();
  if (memcmp(code, kMsgDFileDragData, 4) == 0) {
    uint32_t id = 0;
    uint32_t entry = 0;
    std::string data;
    if (ProtocolUtil::readf(stream, kMsgDFileDragData + 4, &id, &entry, &data)) {
      getServer()->fileDragReceiver().data(id, entry, data);
    }
  } else if (memcmp(code, kMsgDFileDragAck, 4) == 0) {
    uint32_t id = 0;
    uint32_t chunks = 0;
    if (ProtocolUtil::readf(stream, kMsgDFileDragAck + 4, &id, &chunks)) {
      m_sender.ack(id, chunks);
    }
  } else if (memcmp(code, kMsgDFileDragFileEnd, 4) == 0) {
    uint32_t id = 0;
    uint32_t entry = 0;
    if (ProtocolUtil::readf(stream, kMsgDFileDragFileEnd + 4, &id, &entry)) {
      getServer()->fileDragReceiver().fileEnd(id, entry);
    }
  } else if (memcmp(code, kMsgDFileDragBegin, 4) == 0) {
    uint32_t id = 0;
    std::string manifest;
    if (ProtocolUtil::readf(stream, kMsgDFileDragBegin + 4, &id, &manifest)) {
      getServer()->fileDragIncoming(this, id, manifest);
    }
  } else if (memcmp(code, kMsgDFileDragCancel, 4) == 0) {
    uint32_t id = 0;
    uint8_t reason = 0;
    if (ProtocolUtil::readf(stream, kMsgDFileDragCancel + 4, &id, &reason)) {
      // ids from the two directions never overlap (see FileDragSender)
      if (m_sender.owns(id)) {
        m_sender.cancelled(id, reason);
      } else {
        getServer()->fileDragReceiver().cancelled(id, reason);
      }
    }
  } else if (memcmp(code, kMsgDFileDragState, 4) == 0) {
    uint8_t dragging = 0;
    if (ProtocolUtil::readf(stream, kMsgDFileDragState + 4, &dragging)) {
      m_clientDragging = dragging != 0;
      LOG_DEBUG("file drag: \"%s\" %s dragging files", getName().c_str(), m_clientDragging ? "is" : "stopped");
    }
  } else {
    return ClientProxy1_8::parseMessage(code);
  }
  return true;
}
