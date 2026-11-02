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
Sizes the sender's window (in bytes, see kMinChunkCost) from the time chunks
take to be acknowledged, so
the queue file data builds up on the connection (which mouse and keyboard
messages wait behind) stays short while the link is kept busy.

The lowest ack time seen is the link's base delay. Once per round (a
window's worth of acked data) the round's lowest ack time is compared with
it: little extra delay and data waiting for window space -> grow by
kWindowStepBytes; much extra delay -> shrink by a quarter. Taking the
round's lowest sample filters out Wi-Fi jitter. The base delay is kept until
reset(), so if the link gets slower the window only shrinks (towards
kMinWindowBytes), never grows on a stale base.
*/
class WindowControl
{
public:
  //! Extra delay over the base below which the window may grow.
  static constexpr double kGrowBelow = 0.004;
  //! Extra delay over the base above which the window shrinks.
  static constexpr double kShrinkAbove = 0.010;

  //! Current window, in bytes.
  uint32_t window() const
  {
    return static_cast<uint32_t>(m_window);
  }

  //! \p bytes more were acknowledged (as counted against the window), the
  //! last chunk \p ackTime seconds after it was written (negative if
  //! unknown). \p windowLimited: the sender had data waiting for window
  //! space since the previous ack.
  void acked(uint64_t bytes, double ackTime, bool windowLimited);

  //! Back to the starting window, forgetting the base delay.
  void reset();

  //! Lowest ack time seen since reset(), negative if none.
  double baseDelay() const
  {
    return m_haveBase ? m_base : -1;
  }

private:
  double m_window = kWindowBytes;
  bool m_haveBase = false;
  double m_base = 0;
  bool m_haveRoundMin = false;
  double m_roundMin = 0;
  uint64_t m_roundAcked = 0;
  bool m_roundLimited = false;
};

/*!
Sends dragged files to the other computer: walks folders and reads files on
a worker thread per transfer (both can block, e.g. on a macOS privacy
prompt), then writes the manifest and the data chunks to the stream from the
event thread. Several transfers can run at once: they take turns chunk by
chunk, and the number of unacknowledged chunks in flight stays bounded
(by WindowControl, a little more with several transfers so each can reach
the receiver's ack threshold).
*/
class FileDragSender
{
public:
  FileDragSender(IEventQueue *events, deskflow::IStream *stream, uint32_t idBase, bool largeManifests = false);
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

  //! Current window, in bytes.
  uint32_t window() const;

private:
  struct Transfer;

  bool sendBegin(const std::shared_ptr<Transfer> &transfer);
  void finishIfDone(const std::shared_ptr<Transfer> &transfer);
  uint64_t inFlight() const;
  void resetWhenIdle();

  bool m_largeManifests; // negotiated protocol >= 1.11
  IEventQueue *m_events;
  deskflow::IStream *m_stream;
  uint32_t m_idBase;
  uint32_t m_nextId = 1;
  std::vector<std::shared_ptr<Transfer>> m_transfers; // in start order
  size_t m_turn = 0;                                  // round-robin position
  WindowControl m_control;
  bool m_windowLimited = false; // data waited for window space since the last ack
};

} // namespace deskflow::filedrag
