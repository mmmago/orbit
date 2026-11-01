/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include "deskflow/FileDrag.h"

#include <memory>
#include <string>
#include <vector>

class IEventQueue;

namespace deskflow {
class IStream;
}

namespace deskflow::filedrag {

//! Transfer ids: the server numbers its drags from 1, the client from
//! kClientTransferIdBase, so a cancel or ack can never be mistaken for the
//! other direction's transfer.
inline constexpr uint32_t kServerTransferIdBase = 0;
inline constexpr uint32_t kClientTransferIdBase = 0x80000000u;

/*!
Sends dragged files to the other computer: walks folders and reads files on
a worker thread per transfer (both can block, e.g. on a macOS privacy
prompt), then writes the manifest and the data chunks to the stream from the
event thread. Several transfers can run at once: they take turns chunk by
chunk, and the number of unacknowledged chunks in flight stays bounded
(kWindowChunks, a little more with several transfers so each can reach the
receiver's ack threshold).
*/
class FileDragSender
{
public:
  FileDragSender(IEventQueue *events, deskflow::IStream *stream, uint32_t idBase);
  ~FileDragSender();

  FileDragSender(const FileDragSender &) = delete;
  FileDragSender &operator=(const FileDragSender &) = delete;

  //! Starts sending \p paths, alongside any transfer still running.
  //! Returns false if there is nothing to send.
  bool start(const std::vector<std::string> &paths);

  //! True if \p transferId is one of this sender's running transfers.
  bool owns(uint32_t transferId) const;

  //! Number of running transfers.
  size_t activeTransfers() const
  {
    return m_transfers.size();
  }

  //! The receiver acknowledged \p chunks chunks in total.
  void ack(uint32_t transferId, uint32_t chunks);

  //! The receiver ended the transfer.
  void cancelled(uint32_t transferId, uint8_t reason);

  //! Stops one transfer, optionally telling the receiver.
  void stop(uint32_t transferId, CancelReason reason, bool notifyReceiver);

  //! Stops all transfers.
  void stopAll(CancelReason reason, bool notifyReceiver);

  //! Sends whatever the worker has read, within the ack window.
  void pump();

private:
  struct Transfer;

  bool sendBegin(const std::shared_ptr<Transfer> &transfer);
  void finishIfDone(const std::shared_ptr<Transfer> &transfer);
  uint32_t inFlight() const;

  IEventQueue *m_events;
  deskflow::IStream *m_stream;
  uint32_t m_idBase;
  uint32_t m_nextId = 1;
  std::vector<std::shared_ptr<Transfer>> m_transfers; // in start order
  size_t m_turn = 0;                                  // round-robin position
};

} // namespace deskflow::filedrag
