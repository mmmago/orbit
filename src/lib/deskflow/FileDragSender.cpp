/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#include "deskflow/FileDragSender.h"

#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace deskflow::filedrag {

namespace {
// Chunks the worker may read ahead of the network.
constexpr size_t kReadAheadChunks = kWindowChunks * 2;
} // namespace

//! State shared between the event thread and the worker. The worker owns a
//! reference, so it can outlive the sender (e.g. while stuck in open()
//! behind a permission prompt) without touching it.
struct FileDragSender::Transfer
{
  uint32_t id = 0;
  std::mutex mutex;
  std::condition_variable spaceAvailable;
  std::deque<FileReader::Chunk> chunks;
  bool readDone = false;
  bool cancelled = false;
  std::string error;

  // set by the worker once the folders were walked
  bool manifestReady = false;
  Manifest manifest;
  // set by the event thread once the manifest was sent; the worker waits
  // for it before reading, so data never precedes the manifest
  bool beginSent = false;

  // event thread only
  uint32_t chunksSent = 0;
  uint32_t chunksAcked = 0;
  uint64_t bytesSent = 0;
};

FileDragSender::FileDragSender(IEventQueue *events, deskflow::IStream *stream, uint32_t idBase)
    : m_events(events),
      m_stream(stream),
      m_idBase(idBase)
{
  m_events->addHandler(EventTypes::FileDragDataReady, this, [this](const auto &) { pump(); });
}

FileDragSender::~FileDragSender()
{
  m_events->removeHandler(EventTypes::FileDragDataReady, this);
  stopAll(CancelReason::Error, false);
}

bool FileDragSender::owns(uint32_t transferId) const
{
  for (const auto &t : m_transfers) {
    if (t->id == transferId) {
      return true;
    }
  }
  return false;
}

uint32_t FileDragSender::inFlight() const
{
  uint32_t total = 0;
  for (const auto &t : m_transfers) {
    total += t->chunksSent - t->chunksAcked;
  }
  return total;
}

bool FileDragSender::start(const std::vector<std::string> &paths)
{
  if (paths.empty()) {
    return false;
  }

  // runs alongside earlier transfers that are still sending
  auto transfer = std::make_shared<Transfer>();
  transfer->id = m_idBase + m_nextId++;
  m_transfers.push_back(transfer);

  IEventQueue *events = m_events;
  void *target = this;
  std::thread([transfer, events, target, paths]() {
    auto notify = [&] { events->addEvent(Event(EventTypes::FileDragDataReady, target)); };

    std::vector<std::string> sources;
    Manifest manifest = buildManifest(paths, sources);
    {
      std::unique_lock lock(transfer->mutex);
      transfer->manifest = manifest;
      transfer->manifestReady = true;
    }
    notify();
    {
      std::unique_lock lock(transfer->mutex);
      transfer->spaceAvailable.wait(lock, [&] { return transfer->cancelled || transfer->beginSent; });
      if (transfer->cancelled) {
        return;
      }
    }

    FileReader reader(sources, manifest);
    try {
      while (true) {
        {
          std::unique_lock lock(transfer->mutex);
          transfer->spaceAvailable.wait(lock, [&] {
            return transfer->cancelled || transfer->chunks.size() < kReadAheadChunks;
          });
          if (transfer->cancelled) {
            return;
          }
        }
        auto chunk = reader.next(); // may block (permission prompt, slow disk)
        std::scoped_lock lock(transfer->mutex);
        if (transfer->cancelled) {
          return;
        }
        if (!chunk) {
          transfer->readDone = true;
          break;
        }
        transfer->chunks.push_back(std::move(*chunk));
        notify();
      }
    } catch (const std::exception &e) {
      std::scoped_lock lock(transfer->mutex);
      transfer->error = e.what();
    }
    notify();
  }).detach();

  return true;
}

bool FileDragSender::sendBegin(const std::shared_ptr<Transfer> &transfer)
{
  Manifest manifest;
  {
    std::scoped_lock lock(transfer->mutex);
    if (!transfer->manifestReady) {
      return false;
    }
    manifest = transfer->manifest;
  }
  if (manifest.entries.empty()) {
    LOG_INFO("file drag: nothing to send");
    stop(transfer->id, CancelReason::Error, false);
    return false;
  }
  LOG_INFO(
      "file drag: sending %zu item(s), %zu file(s), %llu bytes (transfer %u)", manifest.items().size(),
      manifest.fileCount(), static_cast<unsigned long long>(manifest.totalSize()), transfer->id
  );
  const std::string data = manifest.serialize();
  ProtocolUtil::writef(m_stream, kMsgDFileDragBegin, transfer->id, &data);
  {
    std::scoped_lock lock(transfer->mutex);
    transfer->beginSent = true;
  }
  transfer->spaceAvailable.notify_all();
  return true;
}

void FileDragSender::finishIfDone(const std::shared_ptr<Transfer> &transfer)
{
  bool failed = false;
  bool finished = false;
  {
    std::scoped_lock lock(transfer->mutex);
    failed = !transfer->error.empty();
    finished = transfer->beginSent && transfer->readDone && transfer->chunks.empty();
  }
  if (failed) {
    LOG_ERR("file drag: %s", transfer->error.c_str());
    stop(transfer->id, CancelReason::Error, true);
  } else if (finished) {
    LOG_INFO(
        "file drag: transfer %u fully sent (%llu bytes)", transfer->id,
        static_cast<unsigned long long>(transfer->bytesSent)
    );
    std::erase(m_transfers, transfer);
  }
}

void FileDragSender::pump()
{
  // manifests first: they are small and let the receiver start its drag
  for (const auto &transfer : std::vector(m_transfers)) {
    if (!transfer->beginSent) {
      sendBegin(transfer);
    }
  }

  // With several transfers each needs up to kAckEveryChunks unacknowledged
  // chunks before the receiver acks it, so the window grows with them.
  const auto window = std::max<uint32_t>(kWindowChunks, static_cast<uint32_t>(m_transfers.size()) * kAckEveryChunks);

  // take turns, one chunk per transfer, while the window allows
  bool progress = true;
  while (progress && inFlight() < window && !m_transfers.empty()) {
    progress = false;
    for (size_t n = 0; n < m_transfers.size() && inFlight() < window; ++n) {
      const auto transfer = m_transfers[(m_turn + n) % m_transfers.size()];
      if (!transfer->beginSent) {
        continue;
      }
      FileReader::Chunk chunk;
      {
        std::scoped_lock lock(transfer->mutex);
        if (!transfer->error.empty() || transfer->chunks.empty()) {
          continue;
        }
        chunk = std::move(transfer->chunks.front());
        transfer->chunks.pop_front();
      }
      transfer->spaceAvailable.notify_one();
      progress = true;

      if (!chunk.data.empty()) {
        ProtocolUtil::writef(m_stream, kMsgDFileDragData, transfer->id, chunk.entryIndex, &chunk.data);
        ++transfer->chunksSent;
        transfer->bytesSent += chunk.data.size();
      }
      if (chunk.fileEnd) {
        LOG_DEBUG("file drag: transfer %u entry %u sent", transfer->id, chunk.entryIndex);
        ProtocolUtil::writef(m_stream, kMsgDFileDragFileEnd, transfer->id, chunk.entryIndex);
      }
    }
    if (!m_transfers.empty()) {
      m_turn = (m_turn + 1) % m_transfers.size();
    }
  }

  for (const auto &transfer : std::vector(m_transfers)) {
    finishIfDone(transfer);
  }
}

void FileDragSender::ack(uint32_t transferId, uint32_t chunks)
{
  for (const auto &t : m_transfers) {
    if (t->id == transferId) {
      t->chunksAcked = chunks;
      pump();
      return;
    }
  }
}

void FileDragSender::cancelled(uint32_t transferId, uint8_t reason)
{
  if (!owns(transferId)) {
    return;
  }
  LOG_INFO("file drag: receiver ended transfer %u (reason %u)", transferId, reason);
  stop(transferId, static_cast<CancelReason>(reason), false);
  pump(); // the others may use the freed window
}

void FileDragSender::stop(uint32_t transferId, CancelReason reason, bool notifyReceiver)
{
  const auto it =
      std::find_if(m_transfers.begin(), m_transfers.end(), [&](const auto &t) { return t->id == transferId; });
  if (it == m_transfers.end()) {
    return;
  }
  auto transfer = *it;
  m_transfers.erase(it);
  {
    std::scoped_lock lock(transfer->mutex);
    transfer->cancelled = true;
    transfer->chunks.clear();
  }
  transfer->spaceAvailable.notify_all();
  if (notifyReceiver) {
    ProtocolUtil::writef(m_stream, kMsgDFileDragCancel, transfer->id, static_cast<uint8_t>(reason));
  }
}

void FileDragSender::stopAll(CancelReason reason, bool notifyReceiver)
{
  while (!m_transfers.empty()) {
    stop(m_transfers.front()->id, reason, notifyReceiver);
  }
}

} // namespace deskflow::filedrag
