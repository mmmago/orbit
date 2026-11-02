/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include "client/ServerProxy1_8.h"
#include "deskflow/FileDragSender.h"

//! Proxy for server implementing protocol version 1.9 (file drag and drop)
class ServerProxy1_9 : public ServerProxy1_8
{
public:
  ServerProxy1_9(Client *client, deskflow::IStream *stream, IEventQueue *events, bool largeManifests = false);
  ~ServerProxy1_9() override = default;

  void sendFileDragAck(uint32_t transferId, uint32_t chunksReceived) override;
  void sendFileDragCancel(uint32_t transferId, uint8_t reason) override;
  void sendFileDragState(bool dragging) override;
  bool startFileDrag(const std::vector<std::string> &paths) override;

protected:
  ConnectionResult parseMessage(const uint8_t *code) override;

private:
  deskflow::filedrag::FileDragSender m_sender;
  deskflow::filedrag::ManifestAssembler m_manifestParts;
};
