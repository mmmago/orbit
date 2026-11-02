/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#include "deskflow/FileDragReceiver.h"

#include "arch/Arch.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/Computer.h"

namespace deskflow::filedrag {

FileDragReceiver::FileDragReceiver(IEventQueue *events, deskflow::Computer *computer, std::filesystem::path stagingRoot)
    : m_stagingRoot(std::move(stagingRoot)),
      m_events(events),
      m_computer(computer),
      m_target(computer->getEventTarget())
{
  m_events->addHandler(EventTypes::FileDragEnded, m_target, [this](const auto &e) { handleEnded(e); });
  m_events->addHandler(EventTypes::FileDragCancelRequested, m_target, [this](const auto &e) {
    handleCancelRequested(e);
  });
  m_events->addHandler(EventTypes::FileDragDelivered, m_target, [this](const auto &e) { handleDelivered(e); });
  clearStagingRoot(m_stagingRoot);
}

FileDragReceiver::~FileDragReceiver()
{
  m_events->removeHandler(EventTypes::FileDragEnded, m_target);
  m_events->removeHandler(EventTypes::FileDragCancelRequested, m_target);
  m_events->removeHandler(EventTypes::FileDragDelivered, m_target);
  while (!m_transfers.empty()) {
    stop(m_transfers.begin()->first, false, CancelReason::Error);
  }
}

FileDragReceiver::Transfer *FileDragReceiver::find(uint32_t transferId)
{
  const auto it = m_transfers.find(transferId);
  return it == m_transfers.end() ? nullptr : it->second.get();
}

void FileDragReceiver::refuse(uint32_t transferId, FileDragRemote *remote, CancelReason reason)
{
  if (remote != nullptr) {
    remote->sendFileDragCancel(transferId, static_cast<uint8_t>(reason));
  }
}

bool FileDragReceiver::begin(
    uint32_t transferId, const std::string &manifestData, FileDragRemote *remote, bool preparing
)
{
  // only one drag can be in the user's hand: a transfer whose drag was not
  // dropped yet is replaced; dropped transfers keep receiving
  std::vector<uint32_t> superseded;
  for (const auto &[id, t] : m_transfers) {
    if (t->state != State::Dropped) {
      superseded.push_back(id);
    }
  }
  for (const auto id : superseded) {
    stop(id, true, CancelReason::Superseded);
  }
  if (find(transferId) != nullptr) {
    stop(transferId, false, CancelReason::Superseded);
  }

  auto manifest = Manifest::parse(manifestData);
  if (!manifest) {
    LOG_WARN("file drag: invalid manifest, refusing transfer %u", transferId);
    refuse(transferId, remote, CancelReason::Refused);
    return false;
  }

  if (preparing && manifest->items().size() != manifest->entries.size()) {
    refuse(transferId, remote, CancelReason::Refused);
    return false;
  }

  auto t = std::make_unique<Transfer>();
  t->preparing = preparing;
  t->writer = std::make_unique<FileWriter>(m_stagingRoot, transferId, *manifest);
  if (!t->writer->open()) {
    refuse(transferId, remote, CancelReason::Refused);
    return false;
  }

  const auto items = manifest->items();
  LOG_INFO(
      "file drag: receiving %zu item(s), %zu file(s), %llu bytes (transfer %u, %zu other(s) in progress)", items.size(),
      manifest->fileCount(), static_cast<unsigned long long>(manifest->totalSize()), transferId, m_transfers.size()
  );

  t->id = transferId;
  t->remote = remote;
  t->undelivered = items.size();
  initializeContents(*t);
  m_transfers[transferId] = std::move(t);
  m_newest = transferId;
  return true;
}

void FileDragReceiver::initializeContents(Transfer &t)
{
  const auto &manifest = t.writer->manifest();
  const auto count = manifest.items().size();
  t.itemOfEntry = manifest.itemOfEntry();
  t.filesLeft.assign(count, 0);
  t.itemBytes.assign(count, 0);
  t.progressSent.assign(count, 0.0);
  for (size_t i = 0; i < manifest.entries.size(); ++i) {
    if (manifest.entries[i].isFile())
      ++t.filesLeft[t.itemOfEntry[i]];
  }
}

bool FileDragReceiver::completeManifest(uint32_t transferId, const std::string &data, FileDragRemote *remote)
{
  Transfer *t = find(transferId);
  if (t == nullptr || t->remote != remote || !t->preparing) {
    refuse(transferId, remote, CancelReason::Refused);
    return false;
  }
  const auto manifest = Manifest::parse(data);
  const auto &preview = t->writer->manifest();
  const auto items = manifest ? manifest->items() : std::vector<size_t>();
  bool matches = items.size() == preview.entries.size();
  for (size_t n = 0; matches && n < items.size(); ++n) {
    const auto &entry = manifest->entries[items[n]];
    matches = entry.path == preview.entries[n].path && entry.type == preview.entries[n].type;
  }
  if (!manifest || !matches) {
    LOG_WARN("file drag: file list does not match preview (transfer %u)", transferId);
    stop(transferId, true, CancelReason::Refused);
    return false;
  }
  // The helper only reads staging after itemReady. Delete the preview's
  // writer first, before the replacement creates the complete folder tree.
  t->writer.reset();
  t->writer = std::make_unique<FileWriter>(m_stagingRoot, transferId, *manifest);
  if (!t->writer->open()) {
    stop(transferId, true, CancelReason::Error);
    return false;
  }
  t->preparing = false;
  initializeContents(*t);
  LOG_INFO("file drag: file list ready, %zu file(s) (transfer %u)", manifest->fileCount(), transferId);
  if (t->state != State::Pending)
    reportItemInfo(*t);
  return true;
}

void FileDragReceiver::reportItemInfo(Transfer &t)
{
  const auto &manifest = t.writer->manifest();
  const auto count = manifest.items().size();
  std::vector<uint64_t> sizes(count, 0);
  std::vector<uint32_t> entries(count, 0);
  for (size_t i = 0; i < manifest.entries.size(); ++i) {
    sizes[t.itemOfEntry[i]] += manifest.entries[i].size;
    ++entries[t.itemOfEntry[i]];
  }
  for (uint32_t n = 0; n < count; ++n) {
    m_computer->fileDragItemInfo(t.id, n, sizes[n], entries[n]);
    if (t.filesLeft[n] == 0)
      m_computer->fileDragItemReady(t.id, n);
  }
}

bool FileDragReceiver::startPending() const
{
  const auto it = m_transfers.find(m_newest);
  return it != m_transfers.end() && it->second->state == State::Pending;
}

void FileDragReceiver::startDrag()
{
  Transfer *t = find(m_newest);
  if (t == nullptr || t->state != State::Pending) {
    return;
  }

  const auto &manifest = t->writer->manifest();
  const auto items = manifest.items();
  std::vector<StagedItem> staged;
  std::vector<uint64_t> itemSize(items.size(), 0);
  std::vector<uint32_t> itemEntries(items.size(), 0);
  for (size_t i = 0; i < manifest.entries.size(); ++i) {
    itemSize[t->itemOfEntry[i]] += manifest.entries[i].size;
    ++itemEntries[t->itemOfEntry[i]];
  }
  for (size_t n = 0; n < items.size(); ++n) {
    const auto &entry = manifest.entries[items[n]];
    staged.push_back(
        {entry.path, itemSize[n], entry.isDirectory(), t->writer->entryPath(static_cast<uint32_t>(items[n])).string(),
         itemEntries[n]}
    );
  }

  if (!m_computer->beginFileDrag(t->id, staged)) {
    LOG_WARN("file drag: not supported on this computer, refusing");
    stop(t->id, true, CancelReason::Refused);
    return;
  }
  t->state = State::Dragging;

  // A folder preview is not an empty folder: no promise may be fulfilled
  // until its full manifest and contents have arrived.
  if (!t->preparing)
    reportItemInfo(*t);
}

void FileDragReceiver::reportProgress(Transfer &t, uint32_t item, bool force)
{
  const double now = Arch::time();
  if (t.state == State::Pending || (!force && now - t.progressSent[item] < 0.1)) {
    return;
  }
  t.progressSent[item] = now;
  m_computer->fileDragProgress(t.id, item, t.itemBytes[item]);
}

void FileDragReceiver::data(uint32_t transferId, uint32_t entry, const std::string &bytes)
{
  Transfer *t = find(transferId);
  if (t == nullptr) {
    return;
  }
  if (t->preparing || !t->writer->write(entry, bytes)) {
    LOG_ERR("file drag: failed to store data for entry %u", entry);
    stop(transferId, true, CancelReason::Error);
    return;
  }
  const uint32_t received = t->writer->chunksReceived();
  if (received - t->acked >= kAckEveryChunks && t->remote != nullptr) {
    t->acked = received;
    t->remote->sendFileDragAck(transferId, received);
  }

  const auto item = static_cast<uint32_t>(t->itemOfEntry[entry]);
  t->itemBytes[item] += bytes.size();
  reportProgress(*t, item, false);
}

void FileDragReceiver::fileEnd(uint32_t transferId, uint32_t entry)
{
  Transfer *t = find(transferId);
  if (t == nullptr) {
    return;
  }
  if (t->preparing || !t->writer->finish(entry)) {
    LOG_ERR("file drag: file %u is incomplete", entry);
    stop(transferId, true, CancelReason::Error);
    return;
  }

  const uint32_t received = t->writer->chunksReceived();
  if (received != t->acked && t->remote != nullptr) {
    t->acked = received;
    t->remote->sendFileDragAck(transferId, received);
  }

  const auto item = static_cast<uint32_t>(t->itemOfEntry[entry]);
  if (t->filesLeft[item] > 0 && --t->filesLeft[item] == 0) {
    LOG_DEBUG("file drag: transfer %u item %u received", transferId, item);
    reportProgress(*t, item, true);
    if (t->state != State::Pending) {
      m_computer->fileDragItemReady(transferId, item);
    }
  }
}

bool FileDragReceiver::abortUndropped()
{
  Transfer *t = find(m_newest);
  if (t == nullptr || t->state == State::Dropped) {
    return false;
  }
  const bool started = t->state == State::Dragging;
  LOG_INFO("file drag: transfer %u goes back before the drop", t->id);
  stop(t->id, true, CancelReason::Dropped);
  return started;
}

bool FileDragReceiver::cancelled(uint32_t transferId, uint8_t reason)
{
  if (find(transferId) == nullptr) {
    return false;
  }
  LOG_INFO("file drag: sender stopped transfer %u (reason %u)", transferId, reason);
  stop(transferId, false, static_cast<CancelReason>(reason));
  return true;
}

void FileDragReceiver::remoteGone(FileDragRemote *remote)
{
  std::vector<uint32_t> gone;
  for (auto &[id, t] : m_transfers) {
    if (t->remote == remote) {
      t->remote = nullptr;
      gone.push_back(id);
    }
  }
  for (const auto id : gone) {
    stop(id, false, CancelReason::Error);
  }
}

void FileDragReceiver::handleCancelRequested(const Event &event)
{
  const auto *info = static_cast<FileDragEndedInfo *>(event.getDataObject());
  if (find(info->m_transferId) != nullptr) {
    LOG_INFO("file drag: transfer %u cancelled by the user", info->m_transferId);
    stop(info->m_transferId, true, CancelReason::Dropped);
  }
}

void FileDragReceiver::handleEnded(const Event &event)
{
  const auto *info = static_cast<FileDragEndedInfo *>(event.getDataObject());
  Transfer *t = find(info->m_transferId);
  if (t == nullptr) {
    return;
  }
  if (info->m_accepted) {
    // the drop target pulls the files from the staging folder as they complete
    LOG_INFO("file drag: transfer %u dropped, handing over files", t->id);
    t->state = State::Dropped;
    return;
  }
  LOG_INFO("file drag: transfer %u ended without a drop", t->id);
  stop(t->id, true, CancelReason::Dropped);
}

void FileDragReceiver::handleDelivered(const Event &event)
{
  const auto *info = static_cast<FileDragItemInfo *>(event.getDataObject());
  Transfer *t = find(info->m_transferId);
  if (t == nullptr || t->undelivered == 0) {
    return;
  }
  if (--t->undelivered == 0) {
    LOG_DEBUG("file drag: transfer %u fully handed over", t->id);
    if (t->remote != nullptr && t->writer->chunksReceived() != t->acked) {
      t->remote->sendFileDragAck(t->id, t->writer->chunksReceived());
    }
    m_transfers.erase(t->id); // deletes the (now empty) staging folder
  }
}

void FileDragReceiver::stop(uint32_t transferId, bool notifySender, CancelReason reason)
{
  const auto it = m_transfers.find(transferId);
  if (it == m_transfers.end()) {
    return;
  }
  auto &t = *it->second;
  if (t.state != State::Pending) {
    m_computer->abortFileDrag(t.id);
  }
  if (notifySender && t.remote != nullptr) {
    t.remote->sendFileDragCancel(t.id, static_cast<uint8_t>(reason));
  }
  m_transfers.erase(it);
}

} // namespace deskflow::filedrag
