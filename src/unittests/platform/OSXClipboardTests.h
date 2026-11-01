/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit modifications: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */
#include "base/Log.h"

#include <QTest>

class OSXClipboardTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  // Test are run in order top to bottom
  void open();
  void singleFormat();
  void localBitmap();
  void ignoredClipboard_data();
  void ignoredClipboard();
  void localClipboardAfterIgnored();
  void formatConvert_UTF8();

private:
  Log m_log;
  const std::string m_testString = "deskflow test string";
};
