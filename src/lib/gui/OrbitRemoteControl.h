/*
 * Orbit -- keyboard, mouse and trackpad sharing between Macs
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-04 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include "net/Fingerprint.h"

#include <QHostAddress>
#include <QObject>
#include <QPointer>
#include <QString>

class QSslServer;
class QSslSocket;

//! Asking the Orbit on another computer to use this one's keyboard and mouse
/*!
A computer in server mode takes commands on a TLS port next to its server
port, and advertises it with Bonjour. Another Orbit connects with its own
certificate and asks it to become a client of a server: one JSON line each
way.

    -> {"command":"becomeClient","name":"MacBook","server":"MacBook.local"}
    <- {"ok":true}  or  {"ok":false,"error":"..."}

Who asks is known by the fingerprint of its certificate, the one its server
uses too; the computer asked checks it against the servers it trusts, and
asks its user otherwise (see MainWindow::handleBecomeClientRequest).
*/
namespace orbit::remote {

//! The control port: the server port plus one
quint16 controlPort(quint16 serverPort);

//! The name this computer is reached by on the local network, e.g. my-mac.local
QString localHostName();

//! A request to become a client, waiting for its answer
class Request : public QObject
{
  Q_OBJECT

public:
  Request(QSslSocket *socket, const QString &name, const QString &server, const Fingerprint &fingerprint);

  QString name() const
  {
    return m_name;
  }
  //! The server to connect to, host[:port]; the peer's address when it sent none
  QString server() const
  {
    return m_server;
  }
  //! Fingerprint of the certificate of the computer asking
  Fingerprint fingerprint() const
  {
    return m_fingerprint;
  }

  //! Sends the answer and closes the connection; @p error is shown to the
  //! computer that asked
  void reply(bool ok, const QString &error = {});

private:
  QPointer<QSslSocket> m_socket;
  QString m_name;
  QString m_server;
  Fingerprint m_fingerprint;
  bool m_replied = false;
};

//! Takes requests from other Orbits while listening
class Listener : public QObject
{
  Q_OBJECT

public:
  explicit Listener(QObject *parent = nullptr);
  ~Listener() override;

  //! Listens on @p port with the certificate and key in @p certificatePath
  bool start(quint16 port, const QString &certificatePath);
  void stop();
  bool isListening() const;
  quint16 port() const;

Q_SIGNALS:
  //! The request is deleted once replied to, or when its connection closes
  void becomeClientRequested(orbit::remote::Request *request);

private:
  void handleConnection(QSslSocket *socket);
  void handleLine(QSslSocket *socket);

  QSslServer *m_server = nullptr;
};

//! Asks another Orbit to become a client of this computer
class Sender : public QObject
{
  Q_OBJECT

public:
  explicit Sender(QObject *parent = nullptr);

  bool isBusy() const
  {
    return m_socket != nullptr;
  }

  //! Asks @p host on @p port to connect to @p server; this computer is
  //! known there as @p name
  void sendBecomeClient(
      const QString &host, quint16 port, const QString &name, const QString &server, const QString &certificatePath
  );

  //! Fingerprint of the certificate the other computer answered with
  Fingerprint peerFingerprint() const
  {
    return m_peerFingerprint;
  }

Q_SIGNALS:
  void finished(bool ok, const QString &error);

private:
  void finish(bool ok, const QString &error);

  QSslSocket *m_socket = nullptr;
  QByteArray m_request;
  Fingerprint m_peerFingerprint;
};

} // namespace orbit::remote
