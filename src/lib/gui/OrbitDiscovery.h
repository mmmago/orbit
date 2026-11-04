/*
 * Orbit -- keyboard, mouse and trackpad sharing between Macs
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <map>
#include <memory>

//! Finding Orbit servers on the local network with Bonjour (DNS-SD)
/*!
A computer running Orbit as server advertises an "_orbit._tcp" service named
after the computer. Clients browse for it, so they can list the servers on
the network instead of asking for an address.
*/
namespace orbit::discovery {

inline constexpr auto kServiceType = "_orbit._tcp";

//! An Orbit server found on the network
struct Server
{
  QString name;          //!< computer name (the Bonjour service name)
  QString host;          //!< host name, e.g. my-mac.local
  quint16 port = 0;      //!< server port
  QStringList addresses; //!< IPv4 addresses of the host
  quint16 controlPort = 0; //!< where it takes commands from other Orbits, 0 if it doesn't

  //! What a client connects to: the host, with the port if not the default
  QString address() const;

  //! True if @p address (a host name or IP, optionally with :port) is this server
  bool matches(const QString &address) const;
};

class Handle;

//! Advertises this computer as an Orbit server while active
class Advertiser : public QObject
{
  Q_OBJECT

public:
  explicit Advertiser(QObject *parent = nullptr);
  ~Advertiser() override;

  //! @p controlPort: where this computer takes commands (see OrbitRemoteControl.h), 0 for none
  void start(const QString &name, quint16 port, quint16 controlPort = 0);
  void stop();
  bool isActive() const
  {
    return m_handle != nullptr;
  }

private:
  std::unique_ptr<Handle> m_handle;
};

//! Keeps a list of the Orbit servers on the network while active
class Browser : public QObject
{
  Q_OBJECT

public:
  explicit Browser(QObject *parent = nullptr);
  ~Browser() override;

  void start();
  void stop();
  bool isActive() const
  {
    return m_browse != nullptr;
  }

  //! Resolved servers, sorted by name
  QList<Server> servers() const;

Q_SIGNALS:
  void serversChanged();

private:
  struct Entry;

  void serviceFound(const QString &name, quint32 interfaceIndex, const QString &type, const QString &domain);
  void serviceLost(const QString &name);
  void resolved(const QString &name, const QString &host, quint16 port, quint16 controlPort, quint32 interfaceIndex);
  void addressFound(const QString &name, const QString &address);

  std::unique_ptr<Handle> m_browse;
  std::map<QString, std::unique_ptr<Entry>> m_entries;

  friend struct BrowserCallbacks;
};

} // namespace orbit::discovery
