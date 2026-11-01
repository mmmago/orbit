/*
 * Orbit -- keyboard, mouse and trackpad sharing between Macs
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#include "ArrangementView.h"

#include "gui/ComputerSetupModel.h"

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {

constexpr qreal kPadding = 28;
constexpr qreal kMaxTileWidth = 168;
constexpr qreal kAspect = 1.6; // screens are 16:10
constexpr qreal kScreenRadius = 6;
constexpr qreal kPanelRadius = 12;
constexpr qreal kDragThreshold = 4;

} // namespace

ArrangementView::ArrangementView(ComputerSetupModel *model, QWidget *parent) : QWidget(parent), m_model(model)
{
  setMouseTracking(false);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  const auto refresh = [this] {
    if (!isOccupied(m_selected.x(), m_selected.y()))
      clearSelection();
    update();
  };
  connect(m_model, &ComputerSetupModel::computersChanged, this, refresh);
  connect(m_model, &ComputerSetupModel::modelReset, this, refresh);
  connect(m_model, &ComputerSetupModel::dataChanged, this, refresh);
}

QSize ArrangementView::sizeHint() const
{
  return {600, 280};
}

QSize ArrangementView::minimumSizeHint() const
{
  return {420, 220};
}

bool ArrangementView::isOccupied(int column, int row) const
{
  if (column < 0 || row < 0 || column >= m_model->columnCount() || row >= m_model->rowCount())
    return false;
  return !m_model->computer(column, row).isNull();
}

bool ArrangementView::isSelectedServer() const
{
  return isOccupied(m_selected.x(), m_selected.y()) && m_model->computer(m_selected.x(), m_selected.y()).isServer();
}

void ArrangementView::clearSelection()
{
  if (m_selected == QPoint(-1, -1))
    return;
  m_selected = {-1, -1};
  Q_EMIT selectionChanged();
  update();
}

ArrangementView::Layout ArrangementView::computeLayout() const
{
  int minColumn = m_model->columnCount();
  int maxColumn = -1;
  int minRow = m_model->rowCount();
  int maxRow = -1;
  for (int row = 0; row < m_model->rowCount(); ++row) {
    for (int column = 0; column < m_model->columnCount(); ++column) {
      if (!isOccupied(column, row))
        continue;
      minColumn = std::min(minColumn, column);
      maxColumn = std::max(maxColumn, column);
      minRow = std::min(minRow, row);
      maxRow = std::max(maxRow, row);
    }
  }
  if (maxColumn < 0) { // nothing to show
    minColumn = maxColumn = minRow = maxRow = 0;
  }

  // leave room for one more screen around the arrangement, to drop onto
  const int columns = maxColumn - minColumn + 1;
  const int rows = maxRow - minRow + 1;
  const qreal availableWidth = width() - 2 * kPadding;
  const qreal availableHeight = height() - 2 * kPadding;
  qreal tileWidth = std::min({kMaxTileWidth, availableWidth / (columns + 1), availableHeight / (rows + 1) * kAspect});
  tileWidth = std::max<qreal>(tileWidth, 40);

  Layout layout;
  layout.minColumn = minColumn;
  layout.minRow = minRow;
  layout.tile = QSizeF(tileWidth, tileWidth / kAspect);
  const QSizeF used(columns * layout.tile.width(), rows * layout.tile.height());
  layout.origin = QPointF((width() - used.width()) / 2, (height() - used.height()) / 2);
  return layout;
}

QRectF ArrangementView::cellRect(const Layout &layout, int column, int row) const
{
  return {
      layout.origin.x() + (column - layout.minColumn) * layout.tile.width(),
      layout.origin.y() + (row - layout.minRow) * layout.tile.height(), layout.tile.width(), layout.tile.height()
  };
}

QPoint ArrangementView::cellAt(const QPointF &position) const
{
  const auto layout = computeLayout();
  for (int row = 0; row < m_model->rowCount(); ++row) {
    for (int column = 0; column < m_model->columnCount(); ++column) {
      if (isOccupied(column, row) && cellRect(layout, column, row).contains(position))
        return {column, row};
    }
  }
  return {-1, -1};
}

QPoint ArrangementView::snapCell(const QPointF &tileCenter) const
{
  const auto &layout = m_dragLayout;
  const int column = layout.minColumn + static_cast<int>(std::floor((tileCenter.x() - layout.origin.x()) / layout.tile.width()));
  const int row = layout.minRow + static_cast<int>(std::floor((tileCenter.y() - layout.origin.y()) / layout.tile.height()));
  return {std::clamp(column, 0, m_model->columnCount() - 1), std::clamp(row, 0, m_model->rowCount() - 1)};
}

void ArrangementView::paintScreen(QPainter &painter, const QRectF &rect, int column, int row, bool lifted) const
{
  const auto &computer = m_model->computer(column, row);
  const bool selected = QPoint(column, row) == m_selected;
  const QRectF screen = rect.adjusted(1, 1, -1, -1);

  if (lifted) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 90));
    painter.drawRoundedRect(screen.translated(0, 4), kScreenRadius, kScreenRadius);
  }

  // wallpaper-like gradient: blue for the server, slate for clients
  QLinearGradient wallpaper(screen.topLeft(), screen.bottomLeft());
  if (computer.isServer()) {
    wallpaper.setColorAt(0, QColor(0x4f, 0x8c, 0xd6));
    wallpaper.setColorAt(1, QColor(0x1f, 0x3f, 0x7a));
  } else {
    wallpaper.setColorAt(0, QColor(0x6d, 0x7f, 0x94));
    wallpaper.setColorAt(1, QColor(0x2e, 0x39, 0x47));
  }
  QPainterPath shape;
  shape.addRoundedRect(screen, kScreenRadius, kScreenRadius);
  painter.fillPath(shape, wallpaper);

  // the server's screen has a menu bar, like the main display in macOS
  if (computer.isServer()) {
    painter.save();
    painter.setClipPath(shape);
    painter.fillRect(QRectF(screen.left(), screen.top(), screen.width(), screen.height() * 0.09), QColor(255, 255, 255, 215));
    painter.restore();
  }

  painter.setBrush(Qt::NoBrush);
  if (selected) {
    painter.setPen(QPen(palette().color(QPalette::Highlight), 3));
    painter.drawRoundedRect(screen.adjusted(1.5, 1.5, -1.5, -1.5), kScreenRadius - 1, kScreenRadius - 1);
  } else {
    painter.setPen(QPen(QColor(0, 0, 0, 110), 1));
    painter.drawRoundedRect(screen, kScreenRadius, kScreenRadius);
  }

  // name, and what this computer is
  QFont nameFont = font();
  nameFont.setWeight(QFont::DemiBold);
  painter.setPen(Qt::white);
  painter.setFont(nameFont);
  const QFontMetricsF metrics(nameFont);
  const auto name = metrics.elidedText(computer.name(), Qt::ElideMiddle, screen.width() - 12);
  const QRectF nameRect = screen.adjusted(6, 0, -6, -metrics.height() * 0.4);
  painter.drawText(nameRect, Qt::AlignCenter, name);

  QFont roleFont = font();
  roleFont.setPointSizeF(font().pointSizeF() * 0.85);
  painter.setFont(roleFont);
  painter.setPen(QColor(255, 255, 255, 190));
  const QRectF roleRect = screen.adjusted(6, metrics.height() * 1.2, -6, 0);
  painter.drawText(roleRect, Qt::AlignCenter, computer.isServer() ? tr("Server") : tr("Client"));
}

void ArrangementView::paintEvent(QPaintEvent *)
{
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  // panel
  QColor panel = palette().color(QPalette::Text);
  panel.setAlphaF(0.05);
  painter.setPen(Qt::NoPen);
  painter.setBrush(panel);
  painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kPanelRadius, kPanelRadius);

  const auto layout = m_dragging ? m_dragLayout : computeLayout();

  for (int row = 0; row < m_model->rowCount(); ++row) {
    for (int column = 0; column < m_model->columnCount(); ++column) {
      if (!isOccupied(column, row) || (m_dragging && QPoint(column, row) == m_dragCell))
        continue;
      paintScreen(painter, cellRect(layout, column, row), column, row, false);
    }
  }

  if (m_dragging) {
    const QRectF lifted(m_mousePosition - m_grabOffset, layout.tile);
    const auto target = snapCell(lifted.center());
    painter.setBrush(Qt::NoBrush);
    QColor outline = palette().color(QPalette::Text);
    outline.setAlphaF(0.6);
    painter.setPen(QPen(outline, 1.5, Qt::DashLine));
    painter.drawRoundedRect(cellRect(layout, target.x(), target.y()).adjusted(2, 2, -2, -2), kScreenRadius, kScreenRadius);
    painter.setOpacity(0.92);
    paintScreen(painter, lifted, m_dragCell.x(), m_dragCell.y(), true);
  }
}

void ArrangementView::mousePressEvent(QMouseEvent *event)
{
  if (event->button() != Qt::LeftButton)
    return;
  const auto cell = cellAt(event->position());
  if (cell != m_selected) {
    m_selected = cell;
    Q_EMIT selectionChanged();
    update();
  }
  if (cell.x() < 0)
    return;
  m_pressed = true;
  m_dragCell = cell;
  m_pressPosition = event->position();
  m_dragLayout = computeLayout();
  m_grabOffset = event->position() - cellRect(m_dragLayout, cell.x(), cell.y()).topLeft();
}

void ArrangementView::mouseMoveEvent(QMouseEvent *event)
{
  if (!m_pressed)
    return;
  m_mousePosition = event->position();
  if (!m_dragging && (m_mousePosition - m_pressPosition).manhattanLength() >= kDragThreshold) {
    m_dragging = true;
    setCursor(Qt::ClosedHandCursor);
  }
  if (m_dragging)
    update();
}

void ArrangementView::mouseReleaseEvent(QMouseEvent *event)
{
  if (event->button() != Qt::LeftButton || !m_pressed)
    return;
  m_pressed = false;
  if (!m_dragging)
    return;
  m_dragging = false;
  unsetCursor();

  const QRectF lifted(event->position() - m_grabOffset, m_dragLayout.tile);
  const auto target = snapCell(lifted.center());
  if (target != m_dragCell) {
    // move there, swapping with the computer already in that place
    const int columns = m_model->columnCount();
    auto &computers = m_model->m_computers;
    computers.swapItemsAt(m_dragCell.y() * columns + m_dragCell.x(), target.y() * columns + target.x());
    m_selected = target;
    Q_EMIT m_model->dataChanged(m_model->index(0, 0), m_model->index(m_model->rowCount() - 1, columns - 1));
    Q_EMIT m_model->computersChanged();
    Q_EMIT selectionChanged();
    Q_EMIT arrangementChanged();
  }
  update();
}

void ArrangementView::mouseDoubleClickEvent(QMouseEvent *event)
{
  const auto cell = cellAt(event->position());
  if (cell.x() >= 0)
    Q_EMIT editRequested(cell.x(), cell.y());
}
