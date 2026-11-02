/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include "server/ClientProxy1_9.h"

//! Proxy for client implementing protocol version 1.10 (trackpad gestures)
class ClientProxy1_10 : public ClientProxy1_9
{
public:
  ClientProxy1_10(
      const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events,
      bool largeManifests = false
  );
  ~ClientProxy1_10() override = default;

  void trackpadEvent(const std::string &bytes) override;
  bool supportsTrackpadEvents() const override
  {
    return true;
  }
};
