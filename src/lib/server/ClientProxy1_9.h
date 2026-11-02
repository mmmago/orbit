/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include "deskflow/FileDragReceiver.h"
#include "deskflow/FileDragSender.h"
#include "server/ClientProxy1_8.h"

//! Proxy for client implementing protocol version 1.9 (file drag and drop)
/*!
Sends files dragged from the server to this client, and passes files the
client hands over (dragged from the client to the server) to the server's
receiver.
*/
class ClientProxy1_9 : public ClientProxy1_8, public deskflow::filedrag::FileDragRemote
{
public:
  ClientProxy1_9(
      const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events,
      bool largeManifests = false
  );
  ~ClientProxy1_9() override = default;

  // BaseClientProxy overrides
  bool supportsFileDrag() const override
  {
    return true;
  }
  bool startFileDrag(const std::vector<std::string> &paths) override;
  bool isDraggingFiles() const override
  {
    return m_clientDragging;
  }
  void requestFileDragHandOver() override;

  // FileDragRemote overrides (incoming transfers)
  void sendFileDragAck(uint32_t transferId, uint32_t chunksReceived) override;
  void sendFileDragCancel(uint32_t transferId, uint8_t reason) override;

protected:
  bool parseMessage(const uint8_t *code) override;

  //! Sends whatever has been read of an outgoing transfer.
  void pump()
  {
    m_sender.pump();
  }

private:
  deskflow::filedrag::FileDragSender m_sender;
  deskflow::filedrag::ManifestAssembler m_manifestParts;
  bool m_clientDragging = false;
};
