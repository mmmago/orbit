/*
 * Orbit -- keyboard, mouse and trackpad sharing between Macs
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include <QElapsedTimer>
#include <QWidget>

class QTimer;

//! The Orbit logo in the status bar, showing the connection state
/*!
- Idle: not running or disconnected; plain logo
- Waiting: server waiting for clients; green logo
- Connecting: the orbiting rings turn around the centre; plain logo
- Connected: the orbiting rings turn around the centre; green logo
*/
class OrbitStatusIcon : public QWidget
{
  Q_OBJECT

public:
  enum class State
  {
    Idle,
    Waiting,
    Connecting,
    Connected
  };

  explicit OrbitStatusIcon(QWidget *parent = nullptr);

  void setState(State state);
  State state() const
  {
    return m_state;
  }

  QSize sizeHint() const override;

Q_SIGNALS:
  void stateChanged(OrbitStatusIcon::State state);

public:
  QSize minimumSizeHint() const override;

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  int iconSide() const;

  State m_state = State::Idle;
  QTimer *m_timer;
  QElapsedTimer m_clock;
  qreal m_angle = 0;
};
