/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXClipboardMonitor.h"

#include "base/Log.h"
#include "base/OSXAutoReleasePool.h"
#include "platform/OSXClipboard.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>

using namespace std::chrono_literals;

namespace {
OSXClipboardMonitor::Snapshot snapshot(const IClipboard &source)
{
  OSXClipboardMonitor::Snapshot result;
  if (source.open(0)) {
    for (size_t index = 0; index < result.size(); ++index) {
      const auto format = static_cast<IClipboard::Format>(index);
      if (source.has(format))
        result[index] = source.get(format);
    }
    source.close();
  }
  return result;
}

bool copySnapshot(const OSXClipboardMonitor::Snapshot &source, IClipboard &destination)
{
  if (!destination.open(0))
    return false;
  const bool success = destination.empty();
  if (success) {
    for (size_t index = 0; index < source.size(); ++index) {
      if (source[index])
        destination.add(static_cast<IClipboard::Format>(index), *source[index]);
    }
  }
  destination.close();
  return success;
}

class PasteboardBackend final : public OSXClipboardMonitor::Backend
{
public:
  std::optional<OSXClipboardMonitor::Snapshot> read() override
  {
    const bool changed = m_clipboard.synchronize();
    if (!std::exchange(m_firstRead, false) && !changed)
      return std::nullopt;
    return snapshot(m_clipboard);
  }

  void write(const OSXClipboardMonitor::Snapshot &source) override
  {
    copySnapshot(source, m_clipboard);
  }

private:
  OSXClipboard m_clipboard;
  bool m_firstRead = true;
};
} // namespace

struct OSXClipboardMonitor::State
{
  std::mutex mutex;
  std::condition_variable wake;
  bool stop = false;
  bool finished = false;
  uint64_t generation = 0;
  uint64_t writeGeneration = 0;
  std::shared_ptr<const Snapshot> current = std::make_shared<Snapshot>();
  std::shared_ptr<const Snapshot> pending;
  std::chrono::steady_clock::time_point busySince = std::chrono::steady_clock::now();
};

OSXClipboardMonitor::OSXClipboardMonitor(Factory factory) : m_state(std::make_shared<State>())
{
  if (!factory)
    factory = [] { return std::make_unique<PasteboardBackend>(); };
  m_worker = std::thread([state = m_state, factory = std::move(factory)] { run(state, factory); });
}

OSXClipboardMonitor::~OSXClipboardMonitor()
{
  std::unique_lock lock(m_state->mutex);
  m_state->stop = true;
  m_state->wake.notify_all();
  const bool finished = m_state->wake.wait_for(lock, 50ms, [&] { return m_state->finished; });
  lock.unlock();
  if (finished)
    m_worker.join();
  else
    // A macOS XPC call cannot be cancelled safely. The worker owns its backend
    // and shared state, and never calls back into OSXComputer. Let it finish
    // independently rather than hanging disconnect/quit on the clipboard too.
    m_worker.detach();
}

bool OSXClipboardMonitor::copyTo(IClipboard *destination) const
{
  std::shared_ptr<const Snapshot> current;
  {
    std::scoped_lock lock(m_state->mutex);
    current = m_state->current;
  }
  return copySnapshot(*current, *destination);
}

bool OSXClipboardMonitor::copyFrom(const IClipboard *source)
{
  auto pending = std::make_shared<Snapshot>(snapshot(*source));
  {
    std::scoped_lock lock(m_state->mutex);
    m_state->pending = pending;
    m_state->current = std::move(pending);
    ++m_state->writeGeneration;
    m_observedGeneration = m_state->generation;
  }
  m_state->wake.notify_all();
  return true;
}

bool OSXClipboardMonitor::synchronize()
{
  uint64_t generation;
  bool stalled;
  {
    std::scoped_lock lock(m_state->mutex);
    generation = m_state->generation;
    stalled = m_state->busySince != std::chrono::steady_clock::time_point{} &&
              std::chrono::steady_clock::now() - m_state->busySince > 2s;
  }
  if (stalled && !m_warned)
    LOG_WARN("macOS clipboard service is not responding; clipboard sharing is waiting, input remains active");
  else if (!stalled && m_warned)
    LOG_INFO("macOS clipboard service is responding again");
  m_warned = stalled;
  return std::exchange(m_observedGeneration, generation) != generation;
}

void OSXClipboardMonitor::run(const std::shared_ptr<State> &state, const Factory &factory)
{
  try {
    std::unique_ptr<Backend> backend;
    deskflow::runInAutoReleasePool([&] {
      backend = factory();
      return true;
    });
    for (;;) {
      std::shared_ptr<const Snapshot> pending;
      uint64_t writeGeneration;
      {
        std::scoped_lock lock(state->mutex);
        if (state->stop)
          break;
        pending = std::exchange(state->pending, {});
        writeGeneration = state->writeGeneration;
        state->busySince = std::chrono::steady_clock::now();
      }

      std::optional<Snapshot> incoming;
      deskflow::runInAutoReleasePool([&] {
        if (pending)
          backend->write(*pending);
        incoming = backend->read();
        return true;
      });

      std::unique_lock lock(state->mutex);
      state->busySince = {};
      // A read that began before a network clipboard update must not replace
      // the newer snapshot or announce stale ownership when macOS unblocks.
      if (!state->stop && incoming && writeGeneration == state->writeGeneration && *incoming != *state->current) {
        state->current = std::make_shared<Snapshot>(std::move(*incoming));
        ++state->generation;
      }
      state->wake.wait_for(lock, 250ms, [&] { return state->stop || state->pending; });
    }
  } catch (const std::exception &error) {
    LOG_WARN("macOS clipboard worker stopped: %s", error.what());
  }
  {
    std::scoped_lock lock(state->mutex);
    state->finished = true;
    state->busySince = {};
  }
  state->wake.notify_all();
}
