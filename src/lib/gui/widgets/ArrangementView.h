/*
 * Orbit -- keyboard, mouse and trackpad sharing between Macs
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QWidget>

class ComputerSetupModel;

//! Arranges the computers like macOS arranges displays
/*!
Each computer of the server's grid is drawn as a screen; screens next to each
other share an edge, which is where the cursor crosses. Drag a screen to move
it: it snaps to a grid position, and swaps with the computer already there.
The server's screen has a menu bar. Double-click a screen to edit it.
*/
class ArrangementView : public QWidget
{
  Q_OBJECT

public:
  explicit ArrangementView(ComputerSetupModel *model, QWidget *parent = nullptr);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

  //! Grid cell of the selected computer, or (-1, -1)
  QPoint selectedCell() const
  {
    return m_selected;
  }
  bool isSelectedServer() const;
  void clearSelection();

Q_SIGNALS:
  void editRequested(int column, int row);
  void selectionChanged();
  void arrangementChanged();

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
  struct Layout
  {
    int minColumn = 0;
    int minRow = 0;
    QSizeF tile;
    QPointF origin; //!< top-left of the (minColumn, minRow) cell
  };

  Layout computeLayout() const;
  QRectF cellRect(const Layout &layout, int column, int row) const;
  QPoint cellAt(const QPointF &position) const;
  QPoint snapCell(const QPointF &tileCenter) const;
  bool isOccupied(int column, int row) const;
  void paintScreen(QPainter &painter, const QRectF &rect, int column, int row, bool lifted) const;

  ComputerSetupModel *m_model;
  QPoint m_selected{-1, -1};

  // dragging
  bool m_pressed = false;
  bool m_dragging = false;
  QPoint m_dragCell{-1, -1};
  QPointF m_pressPosition;
  QPointF m_grabOffset; //!< mouse position inside the dragged screen
  QPointF m_mousePosition;
  Layout m_dragLayout; //!< frozen while dragging, so screens don't jump
};
