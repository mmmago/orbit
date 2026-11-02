/*
 * Orbit -- keyboard, mouse and trackpad sharing between Macs
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include <QJsonObject>
#include <QObject>

#include <memory>

//! The menu bar panel (SwiftUI, see gui/panel)
/*!
Orbit is a menu bar app: clicking its menu bar icon shows this panel, which
holds the whole interface. Orbit sends it the state to show with setState();
the panel asks for changes with actionRequested().
*/
class OrbitPanel : public QObject
{
  Q_OBJECT

public:
  explicit OrbitPanel(QObject *parent = nullptr);
  ~OrbitPanel() override;

  //! Put Orbit's icon in the menu bar (macOS); clicking it opens the panel
  void installStatusItem();
  void setState(const QJsonObject &state);
  void toggle();
  void show();
  void hide();

  //! Show Touch ID in the menu bar icon to unlock a client; answered with
  //! touchIdFinished()
  void beginTouchId(const QString &nonce, const QString &reason);
  void cancelTouchId(const QString &nonce);

Q_SIGNALS:
  //! @p name: selectMode, setOption, pickServer, moveComputer, restart, quit
  void actionRequested(const QString &name, const QJsonObject &arguments);
  void touchIdFinished(const QString &nonce, bool success);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
