/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-02 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include <QString>

//! The login password a client types into its lock screen after Touch ID on
//! the server (see deskflow/ScreenUnlock.h). Kept in this Mac's Keychain,
//! readable by Orbit and orbit-core only, without a prompt.
namespace deskflow::gui::screenunlock {

//! True if a password is stored
bool hasPassword();

//! Check \p password against this Mac's login and store it. On failure
//! returns false with a message for the user in \p error.
bool storePassword(const QString &password, QString &error);

//! Forget the stored password
void removePassword();

} // namespace deskflow::gui::screenunlock
