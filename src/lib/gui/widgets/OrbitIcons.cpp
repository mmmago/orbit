/*
 * Orbit -- keyboard, mouse and trackpad sharing between Macs
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OrbitIcons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QTransform>

namespace orbit::icons {

namespace {

// gear drawn in a 64 x 64 box
QPainterPath gearPath()
{
  constexpr int kTeeth = 8;
  constexpr double kCenter = 32;
  constexpr double kBody = 21;
  constexpr double kToothWidth = 11;
  constexpr double kToothOuter = 30;
  constexpr double kHole = 9;

  QPainterPath gear;
  gear.addEllipse(QPointF(kCenter, kCenter), kBody, kBody);
  for (int i = 0; i < kTeeth; ++i) {
    QPainterPath tooth;
    tooth.addRoundedRect(QRectF(-kToothWidth / 2, -kToothOuter, kToothWidth, kToothOuter - kBody + 4), 2.5, 2.5);
    QTransform transform;
    transform.translate(kCenter, kCenter);
    transform.rotate(360.0 / kTeeth * i);
    gear = gear.united(transform.map(tooth));
  }
  QPainterPath hole;
  hole.addEllipse(QPointF(kCenter, kCenter), kHole, kHole);
  return gear.subtracted(hole);
}

} // namespace

QIcon gear(const QColor &color)
{
  static const QPainterPath path = gearPath();
  QIcon icon;
  for (const int size : {16, 18, 22, 24, 32}) {
    for (const int scale : {1, 2}) {
      QPixmap pixmap(size * scale, size * scale);
      pixmap.setDevicePixelRatio(scale);
      pixmap.fill(Qt::transparent);
      QPainter painter(&pixmap);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.scale(size / 64.0, size / 64.0);
      painter.fillPath(path, color);
      painter.end();
      icon.addPixmap(pixmap);
    }
  }
  return icon;
}

} // namespace orbit::icons
