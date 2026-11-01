/*
 * Orbit -- keyboard, mouse and trackpad sharing between Macs
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#include "OrbitLogo.h"

#include <QPainter>
#include <QPainterPath>

namespace orbit::logo {

namespace {

// Logo geometry, in the coordinates of the logo artwork: the mark spans
// 281..969 on both axes.
constexpr double kOrigin = 281;
constexpr double kSize = 688;
const QPointF kCenter(625, 627);
constexpr double kCoreRadius = 105;
constexpr double kCoreWidth = 56;
const QPointF kMoons[] = {{812, 415}, {438, 839}};
constexpr double kMoonRadius = 66;
constexpr double kMoonWidth = 50;
constexpr double kOrbitRadius = 308;
constexpr double kOrbitWidth = 58;
// orbit arcs: start angle and sweep in Qt degrees (counter-clockwise);
// each starts with a round end and ends flat inside a moon, its end well
// within the ring's stroke (no notch at the ring's edge, clear of the hole)
constexpr double kArcSweep = 140;
constexpr double kArcStarts[] = {78, 258};

} // namespace

void paint(QPainter &painter, const QRectF &area, const Colors &colors, qreal rotation)
{
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  painter.translate(area.topLeft());
  painter.scale(area.width() / kSize, area.height() / kSize);
  painter.translate(-kOrigin, -kOrigin);

  // centre of the orbit (the server)
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(colors.core, kCoreWidth));
  painter.drawEllipse(kCenter, kCoreRadius, kCoreRadius);

  // everything else turns around the centre
  painter.translate(kCenter);
  painter.rotate(rotation);
  painter.translate(-kCenter);

  const QRectF orbit(kCenter.x() - kOrbitRadius, kCenter.y() - kOrbitRadius, 2 * kOrbitRadius, 2 * kOrbitRadius);
  painter.setPen(QPen(colors.orbit, kOrbitWidth, Qt::SolidLine, Qt::FlatCap));
  for (const auto start : kArcStarts) {
    QPainterPath arc;
    arc.arcMoveTo(orbit, start);
    arc.arcTo(orbit, start, kArcSweep);
    painter.drawPath(arc);
  }
  painter.setPen(Qt::NoPen);
  painter.setBrush(colors.orbit);
  for (const auto start : kArcStarts) {
    QPainterPath arc;
    arc.arcMoveTo(orbit, start);
    painter.drawEllipse(arc.currentPosition(), kOrbitWidth / 2, kOrbitWidth / 2);
  }

  // orbiting rings (the client)
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(colors.moons, kMoonWidth));
  for (const auto &moon : kMoons) {
    painter.drawEllipse(moon, kMoonRadius, kMoonRadius);
  }

  painter.restore();
}

QColor green(const QPalette &palette)
{
  const bool dark = palette.color(QPalette::Window).lightness() < 128;
  return dark ? QColor(0x30, 0xd1, 0x58) : QColor(0x28, 0xa7, 0x45);
}

} // namespace orbit::logo
