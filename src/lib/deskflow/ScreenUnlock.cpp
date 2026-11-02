/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-02 by Lucas Magotteaux; see NOTICE.md.
 */

#include "deskflow/ScreenUnlock.h"

#include "base/Log.h"
#include "deskflow/ipc/CoreIpc.h"
#include "deskflow/ipc/CoreIpcServer.h"

#include <mutex>
#include <random>

namespace deskflow::screenunlock::menubar {

namespace {

std::mutex s_mutex;
std::string s_nonce;
std::function<void(bool)> s_reply;

std::string newNonce()
{
  std::random_device random;
  static const char *const kHex = "0123456789abcdef";
  std::string nonce;
  for (int i = 0; i < 32; ++i) {
    nonce += kHex[random() & 0xF];
  }
  return nonce;
}

} // namespace

bool available()
{
  return deskflow::core::ipc::CoreIpcServer::instance().hasClients();
}

void request(const std::string &reason, std::function<void(bool success)> reply)
{
  std::string previous;
  std::string nonce = newNonce();
  {
    std::lock_guard lock(s_mutex);
    previous.swap(s_nonce);
    s_nonce = nonce;
    s_reply = std::move(reply);
  }
  if (!previous.empty()) {
    ipcSendToClient(QStringLiteral("touchIdCancel"), QString::fromStdString(previous));
  }
  ipcSendToClient(QStringLiteral("touchIdRequest"), QString::fromStdString(nonce + ":" + reason));
}

void cancel()
{
  std::string nonce;
  {
    std::lock_guard lock(s_mutex);
    nonce.swap(s_nonce);
    s_reply = nullptr;
  }
  if (!nonce.empty()) {
    ipcSendToClient(QStringLiteral("touchIdCancel"), QString::fromStdString(nonce));
  }
}

void deliver(const std::string &nonce, bool success)
{
  std::function<void(bool)> reply;
  {
    std::lock_guard lock(s_mutex);
    if (nonce.empty() || nonce != s_nonce) {
      LOG_DEBUG("screen unlock: menu bar answer for a request no longer pending");
      return;
    }
    s_nonce.clear();
    reply.swap(s_reply);
  }
  if (reply) {
    reply(success);
  }
}

} // namespace deskflow::screenunlock::menubar
