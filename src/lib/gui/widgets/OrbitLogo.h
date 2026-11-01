/*
 * Orbit -- keyboard, mouse and trackpad sharing between Macs
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include <QColor>
#include <QPalette>
#include <QRectF>

class QPainter;

//! Drawing the Orbit logo (see src/apps/res/orbit-appicon.svg)
namespace orbit::logo {

struct Colors
{
  QColor orbit; //!< the two arcs
  QColor core;  //!< the centre ring (the server)
  QColor moons; //!< the two orbiting rings (the client)
};

//! Paint the logo, opaque colours expected, filling the square @p area.
//! @p rotation turns the arcs and orbiting rings around the centre (degrees).
void paint(QPainter &painter, const QRectF &area, const Colors &colors, qreal rotation = 0);

//! The macOS system green, for light or dark backgrounds
QColor green(const QPalette &palette);

} // namespace orbit::logo
