/*
 * Orbit -- keyboard, mouse and trackpad sharing between Macs
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ArrangementViewTests.h"

#include "gui/ComputerSetupModel.h"
#include "gui/config/ComputerList.h"
#include "gui/widgets/ArrangementView.h"

#include <QSignalSpy>

namespace {

constexpr int kColumns = 5;
constexpr int kRows = 3;

// desktop at (2, 0) above the server at (2, 1), as in a two-Mac setup
ComputerList twoMacs()
{
  ComputerList list(kColumns);
  for (int i = 0; i < kColumns * kRows; ++i)
    list.append(Computer());
  Computer server(QStringLiteral("laptop"));
  server.markAsServer();
  list[1 * kColumns + 2] = server;
  list[0 * kColumns + 2] = Computer(QStringLiteral("desktop"));
  return list;
}

QString nameAt(const ComputerList &list, int column, int row)
{
  return list[row * kColumns + column].name();
}

// The view is 600 x 280: with one column and two rows used, screens are
// 119.5 x 74.7 px and the (2, 0) cell starts at (240.25, 65.3).
const QPoint kMiniCenter(300, 103);
const QPoint kServerCenter(300, 177);
const QPoint kRightOfServer(419, 177);

void drag(QWidget &view, const QPoint &from, const QPoint &to)
{
  QTest::mousePress(&view, Qt::LeftButton, {}, from);
  for (int step = 1; step <= 4; ++step)
    QTest::mouseMove(&view, from + (to - from) * step / 4);
  QTest::mouseRelease(&view, Qt::LeftButton, {}, to);
}

} // namespace

void ArrangementViewTests::dragToEmptyCellMoves()
{
  auto list = twoMacs();
  ComputerSetupModel model(list, kColumns, kRows);
  ArrangementView view(&model);
  view.resize(600, 280);
  QSignalSpy changed(&model, &ComputerSetupModel::computersChanged);

  drag(view, kMiniCenter, kRightOfServer);

  QCOMPARE(nameAt(list, 3, 1), QStringLiteral("desktop"));
  QVERIFY(list[0 * kColumns + 2].isNull());
  QCOMPARE(nameAt(list, 2, 1), QStringLiteral("laptop"));
  QCOMPARE(changed.count(), 1);
  QCOMPARE(view.selectedCell(), QPoint(3, 1));
}

void ArrangementViewTests::dragOntoComputerSwaps()
{
  auto list = twoMacs();
  ComputerSetupModel model(list, kColumns, kRows);
  ArrangementView view(&model);
  view.resize(600, 280);

  drag(view, kServerCenter, kMiniCenter);

  QCOMPARE(nameAt(list, 2, 0), QStringLiteral("laptop"));
  QCOMPARE(nameAt(list, 2, 1), QStringLiteral("desktop"));
  QVERIFY(list[0 * kColumns + 2].isServer());
}

void ArrangementViewTests::clickSelects()
{
  auto list = twoMacs();
  ComputerSetupModel model(list, kColumns, kRows);
  ArrangementView view(&model);
  view.resize(600, 280);
  QSignalSpy selection(&view, &ArrangementView::selectionChanged);

  QTest::mouseClick(&view, Qt::LeftButton, {}, kServerCenter);
  QCOMPARE(view.selectedCell(), QPoint(2, 1));
  QVERIFY(view.isSelectedServer());

  QTest::mouseClick(&view, Qt::LeftButton, {}, kMiniCenter);
  QCOMPARE(view.selectedCell(), QPoint(2, 0));
  QVERIFY(!view.isSelectedServer());

  QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(20, 20)); // empty panel
  QCOMPARE(view.selectedCell(), QPoint(-1, -1));
  QCOMPARE(selection.count(), 3);
}

void ArrangementViewTests::smallDragIsAClick()
{
  auto list = twoMacs();
  ComputerSetupModel model(list, kColumns, kRows);
  ArrangementView view(&model);
  view.resize(600, 280);

  drag(view, kMiniCenter, kMiniCenter + QPoint(2, 1));

  QCOMPARE(nameAt(list, 2, 0), QStringLiteral("desktop"));
}

QTEST_MAIN(ArrangementViewTests)
