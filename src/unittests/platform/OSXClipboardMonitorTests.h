/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/Log.h"

#include <QTest>

class OSXClipboardMonitorTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void stalledService_data();
  void stalledService();
  void delayedReadCannotReplaceNetworkClipboard();
  void shutdownWhileServiceIsStalled();
  void externalChangesArePublished();
  void networkWriteDoesNotAnnounceLocalOwnership();

private:
  Log m_log;
};
