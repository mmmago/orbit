/*
 * Orbit -- keyboard, mouse and trackpad sharing between Macs
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#include <QTest>

class ArrangementViewTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void dragToEmptyCellMoves();
  void dragOntoComputerSwaps();
  void clickSelects();
  void smallDragIsAClick();
};
