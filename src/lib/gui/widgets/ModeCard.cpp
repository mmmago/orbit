/*
 * Orbit -- keyboard, mouse and trackpad sharing between Macs
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ModeCard.h"

#include <QEnterEvent>
#include <QPainter>
#include <QPainterPath>

#include "OrbitLogo.h"

namespace {

constexpr int kLogoPixels = 104;
constexpr int kPadding = 18;
constexpr int kSpacing = 14;
constexpr qreal kCornerRadius = 14;

} // namespace

ModeCard::ModeCard(Role role, const QString &text, QWidget *parent) : QAbstractButton(parent), m_role(role)
{
  setText(text);
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_Hover);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setAccessibleName(text.section(QLatin1Char('\n'), 0, 0));
}

void ModeCard::setSelected(bool selected)
{
  if (m_selected == selected)
    return;
  m_selected = selected;
  update();
}

QSize ModeCard::sizeHint() const
{
  const auto lines = text().count(QLatin1Char('\n')) + 1;
  const int textHeight = fontMetrics().lineSpacing() * (lines + 1);
  return {320, kPadding + kLogoPixels + kSpacing + textHeight + kPadding};
}

QSize ModeCard::minimumSizeHint() const
{
  return {220, sizeHint().height()};
}

bool ModeCard::isDark() const
{
  return palette().color(QPalette::Window).lightness() < 128;
}

QColor ModeCard::accent() const
{
  return orbit::logo::green(palette());
}

void ModeCard::enterEvent(QEnterEvent *event)
{
  m_hovered = true;
  update();
  QAbstractButton::enterEvent(event);
}

void ModeCard::leaveEvent(QEvent *event)
{
  m_hovered = false;
  update();
  QAbstractButton::leaveEvent(event);
}

void ModeCard::paintEvent(QPaintEvent *)
{
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  // card
  const QRectF card = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
  QColor fill = palette().color(QPalette::Text);
  fill.setAlphaF(m_hovered ? 0.06 : 0.03);
  QColor border = palette().color(QPalette::Text);
  border.setAlphaF(0.15);
  qreal borderWidth = 1;
  if (m_selected) {
    fill = accent();
    fill.setAlphaF(isDark() ? 0.16 : 0.10);
    border = accent();
    borderWidth = 2.5;
  } else if (hasFocus()) {
    border = palette().color(QPalette::Highlight);
    borderWidth = 2;
  }
  painter.setPen(QPen(border, borderWidth));
  painter.setBrush(fill);
  painter.drawRoundedRect(card, kCornerRadius, kCornerRadius);

  // logo
  const QRectF logo((width() - kLogoPixels) / 2.0, kPadding, kLogoPixels, kLogoPixels);
  paintLogo(painter, logo);

  // text: first line as the title, the rest (the explanation) dimmed
  const QRectF textArea(kPadding, logo.bottom() + kSpacing, width() - 2 * kPadding, height());
  const auto title = text().section(QLatin1Char('\n'), 0, 0);
  const auto detail = text().section(QLatin1Char('\n'), 1);

  QFont titleFont = font();
  titleFont.setWeight(QFont::DemiBold);
  painter.setFont(titleFont);
  painter.setPen(palette().color(QPalette::Text));
  QRectF used;
  painter.drawText(textArea, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, title, &used);

  if (!detail.isEmpty()) {
    QColor dim = palette().color(QPalette::Text);
    dim.setAlphaF(0.6);
    painter.setFont(font());
    painter.setPen(dim);
    painter.drawText(
        textArea.adjusted(0, used.height() + 2, 0, 0), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, detail
    );
  }
}

void ModeCard::paintLogo(QPainter &target, const QRectF &area) const
{
  // Draw the logo opaque into a layer, then fade the whole layer: strokes
  // overlap (round ends, arcs running into the rings), and translucent
  // strokes would show darker spots there.
  const qreal dpr = devicePixelRatioF();
  QImage layer((area.size() * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
  layer.setDevicePixelRatio(dpr);
  layer.fill(Qt::transparent);

  QPainter painter(&layer);
  const QColor neutral = palette().color(QPalette::Text);
  const QColor highlight = (m_selected || m_hovered) ? accent() : neutral;
  orbit::logo::paint(
      painter, QRectF(QPointF(0, 0), area.size()),
      {neutral, m_role == Role::Server ? highlight : neutral, m_role == Role::Client ? highlight : neutral}
  );
  painter.end();

  target.save();
  target.setOpacity(m_selected ? 1.0 : (m_hovered ? 0.75 : 0.45));
  target.drawImage(area.topLeft(), layer);
  target.restore();
}
