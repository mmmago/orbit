/*
 * Orbit -- keyboard, mouse and trackpad sharing between Macs
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-04 by Lucas Magotteaux; see NOTICE.md.
 */

#include "OrbitRemoteControl.h"

#include <QDebug>
#include <QFile>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QTimer>

#ifdef Q_OS_MACOS
#include <SystemConfiguration/SystemConfiguration.h>
#endif

namespace orbit::remote {

namespace {

constexpr auto kBecomeClient = "becomeClient";
constexpr qint64 kMaxLine = 4096;
constexpr int kRequestTimeout = 15 * 1000;
// the user of the other computer may be asked first
constexpr int kAnswerTimeout = 90 * 1000;

//! This computer's certificate and key, for both ends
bool loadIdentity(const QString &path, QSslConfiguration &config)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning().noquote() << "remote control: cannot read the certificate" << path;
    return false;
  }
  const auto pem = file.readAll();
  const auto certificates = QSslCertificate::fromData(pem, QSsl::Pem);
  QSslKey key(pem, QSsl::Rsa, QSsl::Pem);
  if (key.isNull())
    key = QSslKey(pem, QSsl::Ec, QSsl::Pem);
  if (certificates.isEmpty() || key.isNull()) {
    qWarning().noquote() << "remote control: no certificate and key in" << path;
    return false;
  }
  config.setLocalCertificate(certificates.first());
  config.setPrivateKey(key);
  config.setProtocol(QSsl::TlsV1_2OrLater);
  return true;
}

QByteArray jsonLine(const QJsonObject &object)
{
  return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

QString peerHost(const QSslSocket *socket)
{
  const auto address = socket->peerAddress();
  bool isIpv4 = false;
  const auto ipv4 = address.toIPv4Address(&isIpv4);
  return isIpv4 ? QHostAddress(ipv4).toString() : address.toString();
}

} // namespace

quint16 controlPort(quint16 serverPort)
{
  return serverPort == 0xFFFF ? 0 : static_cast<quint16>(serverPort + 1);
}

QString localHostName()
{
#ifdef Q_OS_MACOS
  // the Bonjour name (System Settings > Sharing), not the BSD host name,
  // which other Macs may not resolve
  if (const auto name = SCDynamicStoreCopyLocalHostName(nullptr)) {
    const auto text = QString::fromCFString(name);
    CFRelease(name);
    if (!text.isEmpty())
      return text + QStringLiteral(".local");
  }
#endif
  return QHostInfo::localHostName();
}

//
// Request
//

Request::Request(QSslSocket *socket, const QString &name, const QString &server, const Fingerprint &fingerprint)
    : QObject(socket),
      m_socket(socket),
      m_name(name),
      m_server(server),
      m_fingerprint(fingerprint)
{
}

void Request::reply(bool ok, const QString &error)
{
  if (m_replied)
    return;
  m_replied = true;
  if (m_socket) {
    QJsonObject answer{{QStringLiteral("ok"), ok}};
    if (!ok)
      answer[QStringLiteral("error")] = error;
    m_socket->write(jsonLine(answer));
    m_socket->disconnectFromHost();
  }
}

//
// Listener
//

Listener::Listener(QObject *parent) : QObject(parent)
{
}

Listener::~Listener() = default;

bool Listener::start(quint16 port, const QString &certificatePath)
{
  stop();
  if (port == 0)
    return false;

  auto config = QSslConfiguration::defaultConfiguration();
  if (!loadIdentity(certificatePath, config))
    return false;
  // the certificate of who asks is checked against the trusted servers,
  // not against certificate authorities
  config.setPeerVerifyMode(QSslSocket::QueryPeer);

  m_server = new QSslServer(this);
  m_server->setSslConfiguration(config);
  m_server->setHandshakeTimeout(kRequestTimeout);
  connect(m_server, &QSslServer::sslErrors, this, [](QSslSocket *socket, const QList<QSslError> &) {
    socket->ignoreSslErrors();
  });
  connect(m_server, &QSslServer::errorOccurred, this, [](QSslSocket *, QAbstractSocket::SocketError error) {
    qDebug() << "remote control: a connection failed," << error;
  });
  connect(m_server, &QSslServer::pendingConnectionAvailable, this, [this] {
    while (m_server && m_server->hasPendingConnections()) {
      if (auto *socket = qobject_cast<QSslSocket *>(m_server->nextPendingConnection()))
        handleConnection(socket);
    }
  });

  if (!m_server->listen(QHostAddress::Any, port)) {
    qWarning().noquote() << "remote control: cannot listen on port" << port << m_server->errorString();
    stop();
    return false;
  }
  qInfo().noquote() << "remote control: listening on port" << port;
  return true;
}

void Listener::stop()
{
  if (m_server == nullptr)
    return;
  m_server->close();
  m_server->deleteLater();
  m_server = nullptr;
}

bool Listener::isListening() const
{
  return m_server != nullptr && m_server->isListening();
}

quint16 Listener::port() const
{
  return isListening() ? m_server->serverPort() : 0;
}

void Listener::handleConnection(QSslSocket *socket)
{
  connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
  connect(socket, &QSslSocket::readyRead, this, [this, socket] { handleLine(socket); });
  // one request per connection, soon after it opens
  QTimer::singleShot(kRequestTimeout, socket, [socket] {
    if (socket->findChild<Request *>() == nullptr)
      socket->abort();
  });
  handleLine(socket);
}

void Listener::handleLine(QSslSocket *socket)
{
  if (socket->findChild<Request *>() != nullptr)
    return; // already asked
  if (!socket->canReadLine()) {
    if (socket->bytesAvailable() > kMaxLine)
      socket->abort();
    return;
  }

  const auto line = socket->readLine(kMaxLine);
  const auto json = QJsonDocument::fromJson(line).object();
  const auto refuse = [socket](const QString &error) {
    socket->write(jsonLine({{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}}));
    socket->disconnectFromHost();
  };

  if (json.value(QStringLiteral("command")).toString() != QLatin1String(kBecomeClient)) {
    refuse(tr("Unknown command"));
    return;
  }
  const auto certificate = socket->peerCertificate();
  if (certificate.isNull()) {
    refuse(tr("No certificate"));
    return;
  }

  const auto name = json.value(QStringLiteral("name")).toString().trimmed().left(64);
  auto server = json.value(QStringLiteral("server")).toString().trimmed().left(255);
  if (server.isEmpty())
    server = peerHost(socket);
  const Fingerprint fingerprint{QCryptographicHash::Sha256, certificate.digest(QCryptographicHash::Sha256)};

  qInfo().noquote() << "remote control:" << (name.isEmpty() ? peerHost(socket) : name)
                    << "asks this computer to become a client of" << server;
  Q_EMIT becomeClientRequested(new Request(socket, name, server, fingerprint));
}

//
// Sender
//

Sender::Sender(QObject *parent) : QObject(parent)
{
}

void Sender::sendBecomeClient(
    const QString &host, quint16 port, const QString &name, const QString &server, const QString &certificatePath
)
{
  if (m_socket != nullptr)
    return;

  auto config = QSslConfiguration::defaultConfiguration();
  if (!loadIdentity(certificatePath, config)) {
    Q_EMIT finished(false, tr("This Mac has no certificate"));
    return;
  }
  // who answers doesn't matter: it is only asked to connect here
  config.setPeerVerifyMode(QSslSocket::VerifyNone);

  m_request = jsonLine({
      {QStringLiteral("command"), QLatin1String(kBecomeClient)},
      {QStringLiteral("name"), name},
      {QStringLiteral("server"), server},
  });

  m_peerFingerprint = {};
  m_socket = new QSslSocket(this);
  m_socket->setSslConfiguration(config);
  auto *socket = m_socket;
  connect(socket, &QSslSocket::sslErrors, socket, [socket](const QList<QSslError> &) { socket->ignoreSslErrors(); });
  connect(socket, &QSslSocket::encrypted, this, [this, socket] { socket->write(m_request); });
  connect(socket, &QSslSocket::readyRead, this, [this, socket] {
    if (!socket->canReadLine())
      return;
    const auto answer = QJsonDocument::fromJson(socket->readLine(kMaxLine)).object();
    finish(answer.value(QStringLiteral("ok")).toBool(), answer.value(QStringLiteral("error")).toString());
  });
  connect(socket, &QSslSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
    finish(false, socket->errorString());
  });
  QTimer::singleShot(kAnswerTimeout, socket, [this] { finish(false, tr("No answer")); });

  qInfo().noquote() << "remote control: asking" << host << "to become a client of" << server;
  m_socket->connectToHostEncrypted(host, port);
}

void Sender::finish(bool ok, const QString &error)
{
  if (m_socket == nullptr)
    return;
  m_socket->disconnect(this);
  if (const auto certificate = m_socket->peerCertificate(); !certificate.isNull())
    m_peerFingerprint = {QCryptographicHash::Sha256, certificate.digest(QCryptographicHash::Sha256)};
  m_socket->abort();
  m_socket->deleteLater();
  m_socket = nullptr;
  if (ok)
    qInfo("remote control: the other computer becomes a client");
  else
    qWarning().noquote() << "remote control: the other computer did not become a client:" << error;
  Q_EMIT finished(ok, error);
}

} // namespace orbit::remote
