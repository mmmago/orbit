/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit modifications: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include <QStatusBar>

#include "common/Enums.h"
#include "widgets/OrbitStatusIcon.h"

class QPushButton;
class QLabel;

using ProcessState = deskflow::core::ProcessState;
using ConnectionState = deskflow::core::ConnectionState;

class StatusBar : public QStatusBar
{
  Q_OBJECT
public:
  explicit StatusBar(QWidget *parent = nullptr);
  void setStatus(ConnectionState connectionState, ProcessState processState, bool isServer);
  void setServerClients(const QStringList &clients);
  void setSecurityIconVisible(bool visible);
  void setConnectionInterval(int newInterval);
  bool securityIconVisible() const;
  void updateSecurityInfo(bool encrypted);
  void setSecurityIcon(bool encrypted);
  void setSecurityLevel(const QString &securityLevel);
  void setBtnFingerprintVisible(bool visible);
  void updateFound(const QString &version);
  //! Client: the name of the server, shown once connected
  void setServerName(const QString &name);
  QString statusText() const;
  OrbitStatusIcon::State statusIconState() const;

Q_SIGNALS:
  void requestShowMyFingerprints();
  void requestUpdateVersion();
  //! The status text or the logo state changed
  void statusChanged();

protected:
  void changeEvent(QEvent *e) override;

private:
  void updateText();
  void showConnectedToServer();
  void setStatusText(const QString &text);
  void updateTimerLabel();
  QPushButton *m_btnFingerprint = nullptr;
  QLabel *m_lblSecurityIcon = nullptr; // replaced by m_statusIcon, kept hidden
  OrbitStatusIcon *m_statusIcon = nullptr;
  bool m_securityShown = false;
  bool m_connectedAsClient = false;
  QString m_serverName;
  QLabel *m_lblStatus = nullptr;
  QPushButton *m_btnUpdate = nullptr;
  bool m_encrypted = false;
  QString m_securityLevel;
  int m_connectionInterval = -1;
  QTimer *m_retryTimer = nullptr;
};
