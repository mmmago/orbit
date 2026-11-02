/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-02 by Lucas Magotteaux; see NOTICE.md.
 */

#include <QTest>

class OSXMouseButtonCaptureTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void releaseAfterLeaving_data();
  void releaseAfterLeaving();
  void remoteClickStaysCaptured();
  void releaseMatchesItsOwnButton();
  void cancelKeyDoesNotForgetPress();
};
