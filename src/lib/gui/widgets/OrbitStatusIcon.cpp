/*
 * Orbit -- keyboard, mouse and trackpad sharing between Macs
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OrbitStatusIcon.h"

#include "OrbitLogo.h"

#include <QPainter>
#include <QTimer>

namespace {

constexpr int kLeftPadding = 8;
constexpr int kRightPadding = 4;
constexpr int kFrameInterval = 33;       // ms, about 30 frames per second
constexpr qreal kSecondsPerTurn = 2.4;

} // namespace

OrbitStatusIcon::OrbitStatusIcon(QWidget *parent) : QWidget(parent), m_timer(new QTimer(this))
{
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_timer->setInterval(kFrameInterval);
  connect(m_timer, &QTimer::timeout, this, [this] {
    // counter-clockwise, so each ring leads and its arc trails behind it
    m_angle = -360.0 * (m_clock.elapsed() / 1000.0) / kSecondsPerTurn;
    update();
  });
}

void OrbitStatusIcon::setState(State state)
{
  if (m_state == state)
    return;
  m_state = state;

  const bool turning = state == State::Connecting || state == State::Connected;
  if (turning && !m_timer->isActive()) {
    m_clock.restart();
    m_timer->start();
  } else if (!turning) {
    m_timer->stop();
    m_angle = 0;
  }
  update();
  Q_EMIT stateChanged(state);
}

int OrbitStatusIcon::iconSide() const
{
  return fontMetrics().height() + 4;
}

QSize OrbitStatusIcon::sizeHint() const
{
  return {kLeftPadding + iconSide() + kRightPadding, iconSide()};
}

QSize OrbitStatusIcon::minimumSizeHint() const
{
  return sizeHint();
}

void OrbitStatusIcon::paintEvent(QPaintEvent *)
{
  QPainter painter(this);
  const int side = iconSide();
  const QRectF area(kLeftPadding, (height() - side) / 2.0, side, side);

  const bool green = m_state == State::Waiting || m_state == State::Connected;
  const QColor color = green ? orbit::logo::green(palette()) : palette().color(QPalette::WindowText);
  orbit::logo::paint(painter, area, {color, color, color}, m_angle);
}
