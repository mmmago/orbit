/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit modifications: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include "base/Log.h"

#include <QObject>

class ClientProxyTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void keyDown_data();
  void keyDown();
  void keyRepeat_data();
  void keyRepeat();
  void keyUp_data();
  void keyUp();
  void fileDragRespectsWindowAndAcks();
  void fileDragWindowCountsBytes();
  void fileDragSplitsLargeManifest();
  void fileDragStopsOnCancel();
  void fileDragUnreadableFileCancels();
  void fileDragSendsFolders();
  void fileDragClientStateAndHandOver();
  void fileDragParallelTransfersTakeTurns();
  void trackpadEventMessage();

private:
  Log m_log;
};
