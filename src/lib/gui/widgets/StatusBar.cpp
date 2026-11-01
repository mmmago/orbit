/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "StatusBar.h"
#include "OrbitStatusIcon.h"
#include "common/Constants.h"
#include "common/Settings.h"

#include <QEvent>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

StatusBar::StatusBar(QWidget *parent)
    : QStatusBar{parent},
      m_btnFingerprint{new QPushButton(this)},
      m_lblSecurityIcon{new QLabel(this)},
      m_statusIcon{new OrbitStatusIcon(this)},
      m_lblStatus{new QLabel(this)},
      m_btnUpdate{new QPushButton(this)},
      m_retryTimer{new QTimer(this)}
{
  static const auto btnHeight = height() - 2;
  static const auto btnSize = QSize(btnHeight, btnHeight);
  static const auto iconSize = QSize(fontMetrics().height() + 2, fontMetrics().height() + 2);

  m_btnFingerprint->setFlat(true);
  m_btnFingerprint->setIcon(QIcon::fromTheme(QStringLiteral("fingerprint")));
  m_btnFingerprint->setFixedSize(btnSize);
  m_btnFingerprint->setIconSize(iconSize);
  insertPermanentWidget(0, m_btnFingerprint);
  m_btnFingerprint->setVisible(false); // not shown in Orbit
  connect(m_btnFingerprint, &QPushButton::clicked, this, &StatusBar::requestShowMyFingerprints);

  m_lblSecurityIcon->setVisible(false);
  m_lblSecurityIcon->setFixedSize(iconSize);
  m_lblSecurityIcon->setScaledContents(true);

  // the Orbit logo shows the connection state instead of the security shield
  insertPermanentWidget(1, m_statusIcon);
  connect(m_statusIcon, &OrbitStatusIcon::stateChanged, this, &StatusBar::statusChanged);

  setStatusText(tr("%1 is not running").arg(kAppName));
  insertPermanentWidget(2, m_lblStatus, 1);

  m_btnUpdate->setVisible(false);
  m_btnUpdate->setFlat(true);
  m_btnUpdate->setLayoutDirection(Qt::RightToLeft);
  m_btnUpdate->setIcon(QIcon::fromTheme(QStringLiteral("software-updates-release")));
  m_btnUpdate->setFixedHeight(btnHeight);
  m_btnUpdate->setIconSize(iconSize);
  insertPermanentWidget(3, m_btnUpdate);
  connect(m_btnUpdate, &QPushButton::clicked, this, &StatusBar::requestUpdateVersion);

  m_retryTimer->setInterval(1000);
  m_retryTimer->setSingleShot(false);
  connect(m_retryTimer, &QTimer::timeout, this, &StatusBar::updateTimerLabel);

  updateText();
  adjustSize();
}

// clang-format off
void StatusBar::setStatus(ConnectionState connectionState, ProcessState processState, bool isServer)
{
  using IconState = OrbitStatusIcon::State;
  if (m_retryTimer->isActive())
    m_retryTimer->stop();
  setSecurityIconVisible(false);
  m_connectedAsClient = false;
  m_statusIcon->setState(IconState::Idle);
  switch (processState) {
    using enum ProcessState;
    case Starting:
      m_connectionInterval = -1;
      setStatusText(tr("%1 is starting...").arg(kAppName));
      break;

    case RetryPending:
      m_connectionInterval = -1;
      setStatusText(tr("%1 will retry in a moment...").arg(kAppName));
      break;

    case Stopping:
        m_connectionInterval = -1;
      setStatusText(tr("%1 is stopping...").arg(kAppName));
      break;

    case Stopped:
      m_connectionInterval = -1;
      setStatusText(tr("%1 is not running").arg(kAppName));
      break;

    case Started: {
      switch (connectionState) {
        using enum ConnectionState;

        case Listening: {
          if (isServer) {
            setSecurityIconVisible(true);
            m_statusIcon->setState(IconState::Waiting);
            setStatusText(tr("%1 is waiting for clients").arg(kAppName));
          }
          break;
        }

        case Connecting:
          m_statusIcon->setState(IconState::Connecting);
          updateTimerLabel();
          if (Settings::value(Settings::Client::DynamicConnectionRetry).toBool())
            m_retryTimer->start();
          break;

        case Connected: {
          setSecurityIconVisible(true);
          m_connectionInterval = -1;
          m_statusIcon->setState(IconState::Connected);
          if (!isServer) {
            m_connectedAsClient = true;
            showConnectedToServer();
          }
          break;
        }

        case Disconnected:
          setStatusText(tr("%1 is disconnected").arg(kAppName));
          m_connectionInterval = -1;
          break;
      }
    }
  }
}
// clang-format on
void StatusBar::setServerClients(const QStringList &clients)
{
  if (clients.isEmpty()) {
    m_statusIcon->setState(OrbitStatusIcon::State::Waiting);
    setStatusText(tr("%1 is waiting for clients").arg(kAppName));
    m_lblStatus->setToolTip("");
    return;
  }
  m_statusIcon->setState(OrbitStatusIcon::State::Connected);
  const auto clientCount = static_cast<int>(clients.size());
  static const auto comma = QStringLiteral(", ");
  static const auto newLine = QStringLiteral("\n");
  //: Shown when in server mode and at least 1 client is connected
  //: %1 is replaced by the app name
  //: %2 will be a list of at least one client
  const auto text = tr("%1 is connected to %2").arg(kAppName, clients.join(comma));
  setStatusText(text);

  const auto toolTipString = clientCount == 1 ? "" : tr("Clients:\n %1").arg(clients.join(newLine));
  m_lblStatus->setToolTip(toolTipString);
}

void StatusBar::setSecurityIconVisible(bool visible)
{
  // the shield is not shown; the encryption state is in the logo's tooltip
  m_securityShown = visible;
}

void StatusBar::setStatusText(const QString &text)
{
  m_lblStatus->setText(text);
  Q_EMIT statusChanged();
}

QString StatusBar::statusText() const
{
  return m_lblStatus->text();
}

OrbitStatusIcon::State StatusBar::statusIconState() const
{
  return m_statusIcon->state();
}

void StatusBar::setServerName(const QString &name)
{
  m_serverName = name;
  if (m_connectedAsClient)
    showConnectedToServer();
}

void StatusBar::showConnectedToServer()
{
  const auto name =
      m_serverName.isEmpty() ? Settings::value(Settings::Client::RemoteHost).toString() : m_serverName;
  //: Shown when in client mode and connected; %1 is the app name, %2 the server
  setStatusText(tr("%1 is connected to %2").arg(kAppName, name));
}

void StatusBar::setConnectionInterval(int newInterval)
{
  m_connectionInterval = newInterval;
}

bool StatusBar::securityIconVisible() const
{
  return m_securityShown;
}

void StatusBar::setBtnFingerprintVisible(bool)
{
  // Orbit does not show the local fingerprint button
  m_btnFingerprint->setVisible(false);
}

void StatusBar::updateFound(const QString &version)
{
  m_btnUpdate->setVisible(true);
  m_btnUpdate->setToolTip(tr("A new version v%1 is available").arg(version));
}

void StatusBar::changeEvent(QEvent *e)
{
  QStatusBar::changeEvent(e);
  if (e->type() == QEvent::LanguageChange)
    updateText();
}

void StatusBar::updateText()
{
  m_btnFingerprint->setToolTip(tr("View local fingerprint"));
  m_btnUpdate->setText(tr("Update available"));
  setSecurityLevel(m_securityLevel);
}

void StatusBar::updateTimerLabel()
{
  QString text;
  if (m_connectionInterval < 2 || !Settings::value(Settings::Client::DynamicConnectionRetry).toBool()) {
    text = tr("%1 is connecting...").arg(kAppName);
  } else {
    text = tr("%1 is waiting %2 seconds before the next retry").arg(kAppName, QString::number(m_connectionInterval));
    m_connectionInterval--;
  }
  setStatusText(text);
}

void StatusBar::setSecurityIcon(bool encrypted)
{
  const auto icon = QIcon::fromTheme(encrypted ? QIcon::ThemeIcon::SecurityHigh : QIcon::ThemeIcon::SecurityLow);
  m_lblSecurityIcon->setPixmap(icon.pixmap(QSize(32, 32)));
  m_encrypted = encrypted;
  setSecurityLevel(m_securityLevel);
}

void StatusBar::setSecurityLevel(const QString &securityLevel)
{
  m_securityLevel = securityLevel;
  const auto txt = m_encrypted ? tr("%1 Encryption Enabled").arg(m_securityLevel) : tr("Encryption Disabled");
  m_lblSecurityIcon->setToolTip(txt);
  m_statusIcon->setToolTip(txt);
}
