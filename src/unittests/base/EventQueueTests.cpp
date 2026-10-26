/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "EventQueueTests.h"

#include "base/EventQueue.h"

#include <QTest>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

void EventQueueTests::initTestCase()
{
  m_arch.init();
}

void EventQueueTests::dispatchEvent_noHandler_returnsFalse()
{
  EventQueue events;

  QVERIFY(!events.dispatchEvent(Event(EventTypes::ClientDisconnected, this)));
}

void EventQueueTests::dispatchEvent_noTypeHandler_dispatchesUnknownHandler()
{
  EventQueue events;
  bool fallbackCalled = false;
  events.addHandler(EventTypes::Unknown, this, [&fallbackCalled](const Event &) { fallbackCalled = true; });

  QVERIFY(events.dispatchEvent(Event(EventTypes::ClientDisconnected, this)));
  QVERIFY(fallbackCalled);
}

void EventQueueTests::dispatchEvent_handlerRemovesItself_keepsHandlerAliveUntilReturn()
{
  EventQueue events;
  auto handlerLifetime = std::make_shared<int>(1);
  std::weak_ptr<int> handlerLifetimeObserver = handlerLifetime;
  bool handlerAliveAfterRemoval = false;

  events.addHandler(
      EventTypes::ClientDisconnected, this,
      [this, &events, &handlerLifetimeObserver, &handlerAliveAfterRemoval, handlerLifetime](const Event &) {
        events.removeHandler(EventTypes::ClientDisconnected, this);
        handlerAliveAfterRemoval = handlerLifetime != nullptr && !handlerLifetimeObserver.expired();
      }
  );
  handlerLifetime.reset();

  QVERIFY(events.dispatchEvent(Event(EventTypes::ClientDisconnected, this)));
  QVERIFY(handlerAliveAfterRemoval);
  QVERIFY(handlerLifetimeObserver.expired());
}

void EventQueueTests::waitForReady_afterLoopStarted_returnsImmediately()
{
  // the loop signals readiness once; a caller that only starts waiting
  // afterwards must not miss it (used to block for 10 s, then throw)
  EventQueue events;
  std::atomic<bool> handled = false;
  events.addHandler(EventTypes::ClientDisconnected, this, [&handled](const Event &) { handled = true; });
  std::thread loop([&events] { events.loop(); });

  events.addEvent(Event(EventTypes::ClientDisconnected, this));
  QTRY_VERIFY_WITH_TIMEOUT(handled, 2000); // the loop is running and was ready

  const auto start = std::chrono::steady_clock::now();
  bool threw = false;
  try {
    events.waitForReady();
  } catch (const std::runtime_error &) {
    threw = true;
  }
  const auto waited = std::chrono::steady_clock::now() - start;

  events.addEvent(Event(EventTypes::Quit));
  loop.join();

  QVERIFY(!threw);
  QVERIFY(waited < std::chrono::seconds(1));
}

QTEST_MAIN(EventQueueTests)
