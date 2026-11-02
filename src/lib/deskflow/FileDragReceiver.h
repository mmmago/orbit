/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include "base/Event.h"
#include "deskflow/FileDrag.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class IEventQueue;

namespace deskflow {
class Computer;
}

namespace deskflow::filedrag {

//! The connection an incoming transfer arrives on: where acks and cancels go.
class FileDragRemote
{
public:
  virtual ~FileDragRemote() = default;
  virtual void sendFileDragAck(uint32_t transferId, uint32_t chunksReceived) = 0;
  virtual void sendFileDragCancel(uint32_t transferId, uint8_t reason) = 0;
};

/*!
Receives dragged files from the other computer: stores them in a staging
folder, acknowledges chunks, and drives the local drag (through the platform
computer) whose drop target pulls each item once it is complete.

Several transfers can be in progress: once a drag was dropped its transfer
keeps receiving while the user starts the next drag. A transfer is removed
(and its staging folder deleted) when all its items were handed over, or
when it fails or is cancelled.
*/
class FileDragReceiver
{
public:
  FileDragReceiver(IEventQueue *events, deskflow::Computer *computer);
  ~FileDragReceiver();

  FileDragReceiver(const FileDragReceiver &) = delete;
  FileDragReceiver &operator=(const FileDragReceiver &) = delete;

  //! A manifest arrived. Returns false if the transfer was refused (the
  //! remote is told). The local drag starts with startDrag().
  bool begin(uint32_t transferId, const std::string &manifest, FileDragRemote *remote);

  //! Refuse a transfer without looking at it (e.g. the button was released).
  static void refuse(uint32_t transferId, FileDragRemote *remote, CancelReason reason);

  //! True if a transfer began but its local drag was not started yet.
  bool startPending() const;

  //! Starts the local drag of the transfer that began last.
  void startDrag();

  void data(uint32_t transferId, uint32_t entry, const std::string &bytes);
  void fileEnd(uint32_t transferId, uint32_t entry);

  //! The cursor went back to the sender before the drop: stop the newest
  //! transfer if it was not dropped yet (the sender is told). Returns true if
  //! its local drag had started, and so must be cancelled.
  bool abortUndropped();

  //! The sender stopped. Returns true if it was one of this receiver's transfers.
  bool cancelled(uint32_t transferId, uint8_t reason);

  //! The connection went away: drop its transfers.
  void remoteGone(FileDragRemote *remote);

  //! Number of transfers in progress (for tests and logs).
  size_t transfers() const
  {
    return m_transfers.size();
  }

private:
  enum class State
  {
    Pending,  ///< manifest received, local drag not started yet
    Dragging, ///< local drag in progress
    Dropped   ///< dropped: the drop target pulls items as they complete
  };

  struct Transfer
  {
    uint32_t id = 0;
    FileDragRemote *remote = nullptr;
    std::unique_ptr<FileWriter> writer;
    State state = State::Pending;
    uint32_t acked = 0;
    size_t undelivered = 0;
    std::vector<size_t> itemOfEntry;  // entry index -> item index
    std::vector<size_t> filesLeft;    // per item
    std::vector<uint64_t> itemBytes;  // per item, received so far
    std::vector<double> progressSent; // per item
  };

  Transfer *find(uint32_t transferId);
  void stop(uint32_t transferId, bool notifySender, CancelReason reason);
  void reportProgress(Transfer &t, uint32_t item, bool force);
  void handleEnded(const Event &event);
  void handleCancelRequested(const Event &event);
  void handleDelivered(const Event &event);

  IEventQueue *m_events;
  deskflow::Computer *m_computer;
  void *m_target;
  std::map<uint32_t, std::unique_ptr<Transfer>> m_transfers;
  uint32_t m_newest = 0; // transfer whose drag startDrag() starts
};

} // namespace deskflow::filedrag
