/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-02 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <string>

//! Unlocking a client with Touch ID on the server (see deskflow/ScreenUnlock.h)
namespace deskflow::screenunlock::osx {

//! Server: show the Touch ID glyph in a small panel (the system prompt before
//! macOS 12). \p reply runs on another thread with the
//! answer. Returns the request's number, 0 if Touch ID is not available. A
//! new request dismisses the previous one, whose reply then says false.
uint32_t authenticate(const std::string &reason, std::function<void(uint32_t request, bool success)> reply);

//! Server: dismiss the prompt, if one is up.
void cancelAuthentication();

//! Server: a new request number (never 0), shared by every way of asking
uint32_t nextRequest();

//! Client: true if the session's screen is locked.
bool isScreenLocked();

//! Client: true if the unlock password is in the Keychain (does not read it).
bool hasPassword();

//! Client: read the unlock password from the Keychain, without any prompt.
bool loadPassword(std::string &password);

} // namespace deskflow::screenunlock::osx
