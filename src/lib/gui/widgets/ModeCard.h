/*
 * Orbit -- keyboard, mouse and trackpad sharing between Macs
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include <QAbstractButton>

//! A large selectable card for choosing server or client mode
/*!
Shows the Orbit logo with the text under it. When selected, the card is
outlined in green and the logo part that stands for its mode turns green:
the centre of the orbit for the server, the two orbiting rings for the client.
*/
class ModeCard : public QAbstractButton
{
  Q_OBJECT

public:
  enum class Role
  {
    Server,
    Client
  };

  explicit ModeCard(Role role, const QString &text, QWidget *parent = nullptr);

  void setSelected(bool selected);
  bool isSelected() const
  {
    return m_selected;
  }

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

protected:
  void paintEvent(QPaintEvent *event) override;
  void enterEvent(QEnterEvent *event) override;
  void leaveEvent(QEvent *event) override;

private:
  void paintLogo(QPainter &painter, const QRectF &area) const;
  bool isDark() const;
  QColor accent() const;

  Role m_role;
  bool m_selected = false;
  bool m_hovered = false;
};
