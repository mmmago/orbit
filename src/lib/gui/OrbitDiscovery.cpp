/*
 * Orbit -- keyboard, mouse and trackpad sharing between Macs
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#include "OrbitDiscovery.h"

#include <QDebug>
#include <QSocketNotifier>
#include <QTimer>

#include <algorithm>

#include <arpa/inet.h>
#include <dns_sd.h>
#include <netinet/in.h>

namespace orbit::discovery {

namespace {

constexpr quint16 kDefaultPort = 24800;

QString withoutTrailingDot(const char *name)
{
  auto text = QString::fromUtf8(name);
  if (text.endsWith(QLatin1Char('.')))
    text.chop(1);
  return text;
}

QString hostPart(const QString &address)
{
  // strip ":port", but leave IPv6 addresses alone
  if (address.count(QLatin1Char(':')) == 1)
    return address.section(QLatin1Char(':'), 0, 0);
  return address;
}

} // namespace

//
// Server
//

QString Server::address() const
{
  if (port == 0 || port == kDefaultPort)
    return host;
  return QStringLiteral("%1:%2").arg(host).arg(port);
}

bool Server::matches(const QString &address) const
{
  const auto wanted = hostPart(address.trimmed());
  if (wanted.isEmpty())
    return false;
  if (wanted.compare(host, Qt::CaseInsensitive) == 0)
    return true;
  if (wanted.compare(name, Qt::CaseInsensitive) == 0 ||
      wanted.compare(name + QStringLiteral(".local"), Qt::CaseInsensitive) == 0)
    return true;
  return addresses.contains(wanted);
}

//
// Handle: one DNS-SD operation, driven by the Qt event loop
//

class Handle
{
public:
  explicit Handle(DNSServiceRef ref) : m_ref(ref)
  {
    m_notifier = new QSocketNotifier(DNSServiceRefSockFD(ref), QSocketNotifier::Read);
    QObject::connect(m_notifier, &QSocketNotifier::activated, m_notifier, [ref] {
      if (const auto error = DNSServiceProcessResult(ref); error != kDNSServiceErr_NoError)
        qWarning() << "bonjour: processing a result failed, error" << error;
    });
  }

  ~Handle()
  {
    m_notifier->setEnabled(false);
    m_notifier->deleteLater();
    DNSServiceRefDeallocate(m_ref);
  }

  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;

private:
  DNSServiceRef m_ref;
  QSocketNotifier *m_notifier;
};

//
// Advertiser
//

namespace {

void DNSSD_API onRegistered(
    DNSServiceRef, DNSServiceFlags, DNSServiceErrorType error, const char *name, const char *, const char *, void *
)
{
  if (error != kDNSServiceErr_NoError) {
    qWarning() << "bonjour: advertising failed, error" << error;
    return;
  }
  qInfo().noquote() << "bonjour: advertising this server as" << QString::fromUtf8(name);
}

} // namespace

Advertiser::Advertiser(QObject *parent) : QObject(parent)
{
}

Advertiser::~Advertiser() = default;

void Advertiser::start(const QString &name, quint16 port)
{
  stop();
  DNSServiceRef ref = nullptr;
  const auto utf8 = name.toUtf8();
  const auto error = DNSServiceRegister(
      &ref, 0, kDNSServiceInterfaceIndexAny, utf8.constData(), kServiceType, nullptr, nullptr, htons(port), 0, nullptr,
      onRegistered, nullptr
  );
  if (error != kDNSServiceErr_NoError) {
    qWarning() << "bonjour: could not advertise this server, error" << error;
    return;
  }
  m_handle = std::make_unique<Handle>(ref);
}

void Advertiser::stop()
{
  m_handle.reset();
}

//
// Browser
//

struct Browser::Entry
{
  struct Context
  {
    Browser *browser = nullptr;
    QString name;
  };

  Context context;
  int interfaces = 0; //!< a service can be seen on several interfaces
  Server server;
  bool resolved = false;
  std::unique_ptr<Handle> resolve;
  std::unique_ptr<Handle> addressQuery;
};

struct BrowserCallbacks
{
  static void DNSSD_API browsed(
      DNSServiceRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType error, const char *name,
      const char *type, const char *domain, void *context
  )
  {
    auto *browser = static_cast<Browser *>(context);
    if (error != kDNSServiceErr_NoError) {
      qWarning() << "bonjour: browsing failed, error" << error;
      return;
    }
    if (flags & kDNSServiceFlagsAdd)
      browser->serviceFound(QString::fromUtf8(name), interfaceIndex, QString::fromUtf8(type), QString::fromUtf8(domain));
    else
      browser->serviceLost(QString::fromUtf8(name));
  }

  static void DNSSD_API resolved(
      DNSServiceRef, DNSServiceFlags, uint32_t interfaceIndex, DNSServiceErrorType error, const char *,
      const char *host, uint16_t port, uint16_t, const unsigned char *, void *context
  )
  {
    auto *ctx = static_cast<Browser::Entry::Context *>(context);
    if (error != kDNSServiceErr_NoError) {
      qWarning().noquote() << "bonjour: could not resolve" << ctx->name << "error" << error;
      return;
    }
    ctx->browser->resolved(ctx->name, withoutTrailingDot(host), ntohs(port), interfaceIndex);
  }

  static void DNSSD_API addressed(
      DNSServiceRef, DNSServiceFlags flags, uint32_t, DNSServiceErrorType error, const char *,
      const struct sockaddr *address, uint32_t, void *context
  )
  {
    auto *ctx = static_cast<Browser::Entry::Context *>(context);
    if (error != kDNSServiceErr_NoError || !(flags & kDNSServiceFlagsAdd) || address == nullptr ||
        address->sa_family != AF_INET)
      return;
    char text[INET_ADDRSTRLEN] = {};
    const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(address);
    if (inet_ntop(AF_INET, &ipv4->sin_addr, text, sizeof(text)) != nullptr)
      ctx->browser->addressFound(ctx->name, QString::fromLatin1(text));
  }
};

Browser::Browser(QObject *parent) : QObject(parent)
{
}

Browser::~Browser()
{
  stop();
}

void Browser::start()
{
  if (m_browse)
    return;
  DNSServiceRef ref = nullptr;
  const auto error = DNSServiceBrowse(
      &ref, 0, kDNSServiceInterfaceIndexAny, kServiceType, nullptr, BrowserCallbacks::browsed, this
  );
  if (error != kDNSServiceErr_NoError) {
    qWarning() << "bonjour: could not browse for servers, error" << error;
    return;
  }
  m_browse = std::make_unique<Handle>(ref);
}

void Browser::stop()
{
  m_browse.reset();
  if (!m_entries.empty()) {
    m_entries.clear();
    Q_EMIT serversChanged();
  }
}

QList<Server> Browser::servers() const
{
  QList<Server> list;
  for (const auto &[name, entry] : m_entries) {
    if (entry->resolved)
      list.append(entry->server);
  }
  std::sort(list.begin(), list.end(), [](const Server &a, const Server &b) {
    return a.name.localeAwareCompare(b.name) < 0;
  });
  return list;
}

void Browser::serviceFound(const QString &name, quint32 interfaceIndex, const QString &type, const QString &domain)
{
  auto &entry = m_entries[name];
  if (!entry) {
    entry = std::make_unique<Entry>();
    entry->context = {this, name};
    entry->server.name = name;
  }
  if (entry->interfaces++ > 0 || entry->resolve)
    return;

  DNSServiceRef ref = nullptr;
  const auto error = DNSServiceResolve(
      &ref, 0, interfaceIndex, name.toUtf8().constData(), type.toUtf8().constData(), domain.toUtf8().constData(),
      BrowserCallbacks::resolved, &entry->context
  );
  if (error != kDNSServiceErr_NoError) {
    qWarning().noquote() << "bonjour: could not resolve" << name << "error" << error;
    return;
  }
  entry->resolve = std::make_unique<Handle>(ref);
}

void Browser::serviceLost(const QString &name)
{
  const auto it = m_entries.find(name);
  if (it == m_entries.end() || --it->second->interfaces > 0)
    return;
  const bool wasListed = it->second->resolved;
  m_entries.erase(it);
  if (wasListed)
    Q_EMIT serversChanged();
}

void Browser::resolved(const QString &name, const QString &host, quint16 port, quint32 interfaceIndex)
{
  const auto it = m_entries.find(name);
  if (it == m_entries.end())
    return;
  auto &entry = *it->second;
  entry.server.host = host;
  entry.server.port = port;
  entry.resolved = true;

  // the resolve is done; release it once its callback has returned
  QTimer::singleShot(0, this, [this, name] {
    if (const auto found = m_entries.find(name); found != m_entries.end())
      found->second->resolve.reset();
  });

  // look up the host's IPv4 addresses, to recognise a server configured by IP
  DNSServiceRef ref = nullptr;
  if (DNSServiceGetAddrInfo(
          &ref, 0, interfaceIndex, kDNSServiceProtocol_IPv4, host.toUtf8().constData(), BrowserCallbacks::addressed,
          &entry.context
      ) == kDNSServiceErr_NoError) {
    entry.addressQuery = std::make_unique<Handle>(ref);
  }

  Q_EMIT serversChanged();
}

void Browser::addressFound(const QString &name, const QString &address)
{
  const auto it = m_entries.find(name);
  if (it == m_entries.end() || it->second->server.addresses.contains(address))
    return;
  it->second->server.addresses.append(address);
  Q_EMIT serversChanged();
}

} // namespace orbit::discovery
