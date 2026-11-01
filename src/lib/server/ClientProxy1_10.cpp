/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_10.h"

#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

ClientProxy1_10::ClientProxy1_10(
    const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events
)
    : ClientProxy1_9(name, adoptedStream, server, events)
{
}

void ClientProxy1_10::trackpadEvent(const std::string &bytes)
{
  ProtocolUtil::writef(getStream(), kMsgDTrackpadEvent, &bytes);
}
