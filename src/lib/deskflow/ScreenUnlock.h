/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-02 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include "base/Event.h"

#include <cstdint>

/*!
Unlocking a locked client with Touch ID on the server.

The client tells the server when its screen locks (kMsgDScreenLockState) and
offers a one-time token if its user stored the login password in the client's
own Keychain. When the cursor enters that client the server asks for Touch ID
and sends the token back (kMsgCScreenUnlock); the client then types its
password into its lock screen. The password never leaves the client.
*/
namespace deskflow::screenunlock {

//! Keychain item (generic password) holding the client's login password
inline constexpr const char *kKeychainService = "Orbit screen unlock";

//! kMsgDScreenLockState states
enum State : uint8_t
{
  Unlocked = 0,
  Locked = 1,
  //! locked, and the server may unlock it with the token
  Unlockable = 2,
};

//! Event data of EventTypes::PrimaryComputerUserAuthenticated
class UserAuthenticationData : public EventData
{
public:
  UserAuthenticationData(uint32_t request, bool success) : m_request(request), m_success(success)
  {
  }
  //! the number requestUserAuthentication() returned
  uint32_t m_request;
  bool m_success;
};

} // namespace deskflow::screenunlock

#include <functional>
#include <string>

//! Touch ID shown in the menu bar icon (kOptionTouchIdInMenuBar): the icon
//! belongs to the gui, so the core asks it over the core ipc socket
//! ("touchIdRequest=<nonce>:<reason>", "touchIdCancel=<nonce>") and the gui
//! answers "touchIdResult=<nonce>:<0|1>".
namespace deskflow::screenunlock::menubar {

//! True if the gui is connected to answer
bool available();

//! Ask the gui; \p reply runs on the ipc (main) thread. Dismisses a previous
//! request without replying to it.
void request(const std::string &reason, std::function<void(bool success)> reply);

//! Dismiss the pending request, without replying to it
void cancel();

//! The gui answered (called by the core ipc server)
void deliver(const std::string &nonce, bool success);

} // namespace deskflow::screenunlock::menubar
