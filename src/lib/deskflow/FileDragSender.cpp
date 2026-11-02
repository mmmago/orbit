/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#include "deskflow/FileDragSender.h"

#include "arch/Arch.h"
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
// Chunks the worker may read ahead of the network (4 MB per transfer). The
// worker refills as chunks go out, so this need not cover the whole window.
constexpr size_t kReadAheadChunks = 64;
} // namespace

void WindowControl::acked(uint64_t bytes, double ackTime, bool windowLimited)
{
  if (ackTime >= 0) {
    if (!m_haveBase || ackTime < m_base) {
      m_base = ackTime;
      m_haveBase = true;
    }
    if (!m_haveRoundMin || ackTime < m_roundMin) {
      m_roundMin = ackTime;
      m_haveRoundMin = true;
    }
  }
  m_roundAcked += bytes;
  m_roundLimited = m_roundLimited || windowLimited;
  if (m_roundAcked < window()) {
    return;
  }

  // end of a round
  if (m_haveRoundMin) {
    const double extra = m_roundMin - m_base;
    if (extra > kShrinkAbove) {
      m_window = std::max<double>(kMinWindowBytes, m_window * 0.75);
    } else if (extra < kGrowBelow && m_roundLimited) {
      m_window = std::min<double>(kMaxWindowBytes, m_window + kWindowStepBytes);
    }
  }
  m_haveRoundMin = false;
  m_roundAcked = 0;
  m_roundLimited = false;
}

void WindowControl::reset()
{
  *this = WindowControl();
}

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
  bool previewReady = false;
  Manifest preview;
  bool previewSent = false; // event thread only
  bool manifestReady = false;
  Manifest manifest;
  // set by the event thread once the manifest was sent; the worker waits
  // for it before reading, so data never precedes the manifest
  bool beginSent = false;

  // event thread only
  uint32_t chunksSent = 0;
  uint32_t chunksAcked = 0;
  uint64_t bytesSent = 0;
  // counted against the window (see kMinChunkCost)
  uint64_t costSent = 0;
  uint64_t costAcked = 0;
  struct Sent
  {
    uint32_t chunks; // chunksSent after this chunk
    uint64_t cost;   // costSent after this chunk
    double time;     // when it was written
  };
  std::deque<Sent> sent; // until acknowledged
  double beginTime = 0;  // when the manifest was sent
};

FileDragSender::FileDragSender(IEventQueue *events, deskflow::IStream *stream, uint32_t idBase, bool largeManifests)
    : m_largeManifests(largeManifests),
      m_events(events),
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

uint32_t FileDragSender::window() const
{
  // With several transfers each needs up to kAckEveryChunks unacknowledged
  // chunks before the receiver acks it, so the window grows with them.
  return std::max<uint32_t>(
      m_control.window(), static_cast<uint32_t>(m_transfers.size()) * kAckEveryChunks * kChunkSize
  );
}

uint64_t FileDragSender::inFlight() const
{
  uint64_t total = 0;
  for (const auto &t : m_transfers) {
    total += t->costSent - t->costAcked;
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
  std::thread([transfer, events, target, paths, largeManifests = m_largeManifests]() {
    auto notify = [&] { events->addEvent(Event(EventTypes::FileDragDataReady, target)); };

    std::vector<std::string> sources;
    // Inspect only the selected items first. Folder promises must be on the
    // receiving screen while the button is still held, even for a slow scan.
    Manifest preview = buildManifest(paths, sources, false);
    if (largeManifests &&
        std::any_of(preview.entries.begin(), preview.entries.end(), [](const Entry &e) { return e.isDirectory(); })) {
      {
        std::scoped_lock lock(transfer->mutex);
        transfer->preview = std::move(preview);
        transfer->previewReady = true;
      }
      notify();
    }
    Manifest manifest = buildManifest(paths, sources);
    {
      std::unique_lock lock(transfer->mutex);
      if (transfer->cancelled)
        return;
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
    if (transfer->previewReady) {
      const auto preview = transfer->preview.serialize();
      if (preview.size() <= kManifestPartSize) {
        ProtocolUtil::writef(m_stream, kMsgDFileDragPreview, transfer->id, &preview);
        transfer->previewSent = true;
        LOG_INFO(
            "file drag: announced %zu item(s) before scanning (transfer %u)", transfer->preview.entries.size(),
            transfer->id
        );
      }
      transfer->previewReady = false;
    }
    if (!transfer->manifestReady) {
      return false;
    }
    manifest = transfer->manifest;
  }
  if (manifest.entries.empty()) {
    LOG_INFO("file drag: nothing to send");
    stop(transfer->id, CancelReason::Error, transfer->previewSent);
    return false;
  }
  LOG_INFO(
      "file drag: sending %zu item(s), %zu file(s), %llu bytes (transfer %u)", manifest.items().size(),
      manifest.fileCount(), static_cast<unsigned long long>(manifest.totalSize()), transfer->id
  );
  const std::string data = manifest.serialize();
  if (data.size() > kMaxManifestSize) {
    LOG_ERR("file drag: too many files to send at once (%zu bytes of file list)", data.size());
    stop(transfer->id, CancelReason::Error, transfer->previewSent);
    return false;
  }
  if (!m_largeManifests) {
    if (data.size() > PROTOCOL_MAX_STRING_LENGTH) {
      LOG_WARN("file drag: peer needs protocol 1.11 for this folder's file list");
      stop(transfer->id, CancelReason::Refused, false);
      return false;
    }
    ProtocolUtil::writef(m_stream, kMsgDFileDragBegin, transfer->id, &data);
  } else {
    // a protocol string is limited to 1 MB: large manifests go out in parts
    size_t pos = 0;
    for (; data.size() - pos > kManifestPartSize; pos += kManifestPartSize) {
      const std::string part = data.substr(pos, kManifestPartSize);
      ProtocolUtil::writef(m_stream, kMsgDFileDragManifestPart, transfer->id, &part);
    }
    const std::string last = data.substr(pos);
    ProtocolUtil::writef(
        m_stream, transfer->previewSent ? kMsgDFileDragManifest : kMsgDFileDragBegin, transfer->id, &last
    );
  }
  transfer->beginTime = Arch::time();
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
  // its unacknowledged chunks still count against the window (and the
  // receiver acks at every file end, so the last ack always comes)
  finished = finished && transfer->chunksAcked == transfer->chunksSent;
  if (failed) {
    LOG_ERR("file drag: %s", transfer->error.c_str());
    stop(transfer->id, CancelReason::Error, true);
  } else if (finished) {
    const double seconds = Arch::time() - transfer->beginTime;
    LOG_INFO(
        "file drag: transfer %u fully sent (%llu bytes in %.1f s, %.1f MB/s, window %u KB, base delay %.1f ms)",
        transfer->id, static_cast<unsigned long long>(transfer->bytesSent), seconds,
        seconds > 0 ? static_cast<double>(transfer->bytesSent) / 1e6 / seconds : 0.0, m_control.window() / 1024,
        m_control.baseDelay() * 1000
    );
    std::erase(m_transfers, transfer);
    resetWhenIdle();
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

  const auto window = this->window();

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
        transfer->costSent += std::max<uint64_t>(chunk.data.size(), kMinChunkCost);
        transfer->sent.push_back({transfer->chunksSent, transfer->costSent, Arch::time()});
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

  if (inFlight() >= window) {
    for (const auto &transfer : m_transfers) {
      std::scoped_lock lock(transfer->mutex);
      if (transfer->beginSent && !transfer->chunks.empty()) {
        m_windowLimited = true;
        break;
      }
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
      if (chunks <= t->chunksAcked || chunks > t->chunksSent) {
        return; // stale or invalid
      }
      // every chunk sent is recorded, so the acked count is always found
      double ackTime = -1;
      uint64_t costAcked = t->costAcked;
      while (!t->sent.empty() && t->sent.front().chunks <= chunks) {
        if (t->sent.front().chunks == chunks) {
          ackTime = Arch::time() - t->sent.front().time;
        }
        costAcked = t->sent.front().cost;
        t->sent.pop_front();
      }
      m_control.acked(costAcked - t->costAcked, ackTime, m_windowLimited);
      m_windowLimited = false;
      t->chunksAcked = chunks;
      t->costAcked = costAcked;
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
  resetWhenIdle();
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

void FileDragSender::resetWhenIdle()
{
  // the next drag may go over another route (or the same one less busy)
  if (m_transfers.empty()) {
    m_control.reset();
    m_windowLimited = false;
  }
}

void FileDragSender::stopAll(CancelReason reason, bool notifyReceiver)
{
  while (!m_transfers.empty()) {
    stop(m_transfers.front()->id, reason, notifyReceiver);
  }
}

} // namespace deskflow::filedrag
