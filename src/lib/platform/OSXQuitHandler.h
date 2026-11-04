/*
 * Orbit -- keyboard, mouse and trackpad sharing between Macs
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-04 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Forward native helper quits to the GUI. Without a GUI, call quit(context)
// to stop the standalone helper through its own shutdown path.
void installCocoaQuitHandler(void (*quit)(void *), void *context);

#ifdef __cplusplus
}
#endif
