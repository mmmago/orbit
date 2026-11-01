/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include "client/ServerProxy1_9.h"

//! Proxy for server implementing protocol version 1.10 (trackpad gestures)
class ServerProxy1_10 : public ServerProxy1_9
{
public:
  ServerProxy1_10(Client *client, deskflow::IStream *stream, IEventQueue *events);
  ~ServerProxy1_10() override = default;

protected:
  ConnectionResult parseMessage(const uint8_t *code) override;

private:
  void trackpadEvent();
};
