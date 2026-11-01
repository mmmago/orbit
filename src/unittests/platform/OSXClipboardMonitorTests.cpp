/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#include "OSXClipboardMonitorTests.h"

#include "deskflow/Clipboard.h"
#include "platform/OSXClipboardMonitor.h"

#include <QScopeGuard>

#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <utility>

using namespace std::chrono_literals;

namespace {
using Monitor = OSXClipboardMonitor;

struct Service
{
  std::mutex mutex;
  std::condition_variable changed;
  QString blockedStage;
  bool entered = false;
  bool released = false;
  bool destroyed = false;
  int reads = 0;
  int writes = 0;
  std::optional<Monitor::Snapshot> next;
  Monitor::Snapshot written;

  void stall(std::unique_lock<std::mutex> &lock, const QString &stage)
  {
    if (blockedStage == stage) {
      entered = true;
      changed.notify_all();
      changed.wait(lock, [&] { return released; });
    }
  }

  void release()
  {
    std::scoped_lock lock(mutex);
    released = true;
    changed.notify_all();
  }

  template <class Predicate> bool wait(Predicate predicate)
  {
    std::unique_lock lock(mutex);
    return changed.wait_for(lock, 2s, predicate);
  }
};

class Backend final : public Monitor::Backend
{
public:
  explicit Backend(std::shared_ptr<Service> service) : m_service(std::move(service))
  {
    std::unique_lock lock(m_service->mutex);
    m_service->stall(lock, "create");
  }

  ~Backend() override
  {
    std::scoped_lock lock(m_service->mutex);
    m_service->destroyed = true;
    m_service->changed.notify_all();
  }

  std::optional<Monitor::Snapshot> read() override
  {
    std::unique_lock lock(m_service->mutex);
    auto result = std::exchange(m_service->next, {});
    m_service->stall(lock, "read");
    ++m_service->reads;
    m_service->changed.notify_all();
    return result;
  }

  void write(const Monitor::Snapshot &value) override
  {
    std::unique_lock lock(m_service->mutex);
    m_service->stall(lock, "write");
    m_service->written = value;
    ++m_service->writes;
    m_service->changed.notify_all();
  }

private:
  std::shared_ptr<Service> m_service;
};

Monitor::Factory factory(const std::shared_ptr<Service> &service)
{
  return [service] { return std::make_unique<Backend>(service); };
}

void setText(Clipboard &clipboard, const std::string &text)
{
  clipboard.open(0);
  clipboard.empty();
  clipboard.add(IClipboard::Format::Text, text);
  clipboard.close();
}
} // namespace

void OSXClipboardMonitorTests::stalledService_data()
{
  QTest::addColumn<QString>("stage");
  QTest::newRow("pasteboard-creation") << QString("create");
  QTest::newRow("pasteboard-synchronization-and-read") << QString("read");
  QTest::newRow("pasteboard-write") << QString("write");
}

void OSXClipboardMonitorTests::stalledService()
{
  QFETCH(QString, stage);
  auto service = std::make_shared<Service>();
  service->blockedStage = stage;
  const auto cleanup = qScopeGuard([&] { service->release(); });
  Monitor monitor(factory(service));
  Clipboard incoming;
  setText(incoming, "network text");
  if (stage == "write")
    QVERIFY(monitor.copyFrom(&incoming));
  QVERIFY(service->wait([&] { return service->entered; }));

  // Model the input thread while the OS is deliberately held indefinitely.
  auto input = std::async(std::launch::async, [&] {
    monitor.synchronize();
    monitor.copyFrom(&incoming);
    Clipboard copy;
    monitor.copyTo(&copy);
    copy.open(0);
    const auto result = copy.get(IClipboard::Format::Text);
    copy.close();
    return result;
  });
  const auto status = input.wait_for(500ms);
  service->release();
  QVERIFY(status == std::future_status::ready);
  QCOMPARE(input.get(), "network text");
}

void OSXClipboardMonitorTests::delayedReadCannotReplaceNetworkClipboard()
{
  auto service = std::make_shared<Service>();
  service->blockedStage = "read";
  Monitor::Snapshot stale;
  stale[0] = "old local text";
  service->next = stale;
  const auto cleanup = qScopeGuard([&] { service->release(); });
  Monitor monitor(factory(service));
  QVERIFY(service->wait([&] { return service->entered; }));

  Clipboard incoming;
  setText(incoming, "new network text");
  QVERIFY(monitor.copyFrom(&incoming));
  service->release();
  QVERIFY(service->wait([&] { return service->writes > 0 && service->reads > 1; }));
  QVERIFY(!monitor.synchronize());
  Clipboard copy;
  QVERIFY(monitor.copyTo(&copy));
  QVERIFY(copy.open(0));
  QCOMPARE(copy.get(IClipboard::Format::Text), "new network text");
  copy.close();
}

void OSXClipboardMonitorTests::shutdownWhileServiceIsStalled()
{
  auto service = std::make_shared<Service>();
  service->blockedStage = "read";
  const auto cleanup = qScopeGuard([&] { service->release(); });
  auto monitor = std::make_unique<Monitor>(factory(service));
  QVERIFY(service->wait([&] { return service->entered; }));
  auto shutdown = std::async(std::launch::async, [&] { monitor.reset(); });
  const auto status = shutdown.wait_for(500ms);
  service->release();
  QVERIFY(status == std::future_status::ready);
  shutdown.get();
  QVERIFY(service->wait([&] { return service->destroyed; }));
}

void OSXClipboardMonitorTests::externalChangesArePublished()
{
  auto service = std::make_shared<Service>();
  Monitor::Snapshot local;
  local[0] = "local text";
  local[1] = "<b>local text</b>";
  local[2] = "bitmap data";
  service->next = local;
  Monitor monitor(factory(service));
  bool changed = false;
  const auto observe = [&] {
    changed = monitor.synchronize() || changed;
    return changed;
  };
  QTRY_VERIFY(observe());
  QVERIFY(!monitor.synchronize());
  Clipboard copy;
  QVERIFY(monitor.copyTo(&copy));
  QVERIFY(copy.open(0));
  for (size_t index = 0; index < local.size(); ++index) {
    const auto format = static_cast<IClipboard::Format>(index);
    QVERIFY(copy.has(format));
    QCOMPARE(copy.get(format), *local[index]);
  }
  copy.close();
}

void OSXClipboardMonitorTests::networkWriteDoesNotAnnounceLocalOwnership()
{
  auto service = std::make_shared<Service>();
  Monitor::Snapshot local;
  local[0] = "local text";
  service->next = local;
  Monitor monitor(factory(service));
  QVERIFY(service->wait([&] { return service->reads > 0; }));
  Clipboard incoming;
  setText(incoming, "network text");
  QVERIFY(monitor.copyFrom(&incoming));
  QVERIFY(service->wait([&] { return service->writes > 0; }));
  QVERIFY(!monitor.synchronize());
}

QTEST_GUILESS_MAIN(OSXClipboardMonitorTests)
