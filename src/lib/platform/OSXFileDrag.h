/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

//! macOS side of cross-computer file drag and drop.
namespace deskflow::osx::filedrag {

//! Value stored in kCGEventSourceUserData of events Deskflow posts for drag
//! handling, so its own event tap lets them through untouched.
inline constexpr int64_t kSyntheticEventTag = 0x44464447; // 'DFDG'

//
// Source side (the computer where the drag starts)
//

//! Current change count of the system drag pasteboard.
int64_t dragPasteboardChangeCount();

//! Paths of the file URLs on the drag pasteboard (empty if none).
std::vector<std::string> draggedFilePaths();

//! Cancels the local drag session by posting a tagged Escape key press.
void postCancelKey();

//
// Receiving side
//

struct Item
{
  std::string name;
  uint64_t size = 0;       ///< Total bytes (all files of a folder)
  std::string stagingPath; ///< Where the file or folder will be once received
  bool isDirectory = false;
  uint32_t entryCount = 1; ///< Files and folders in this item, itself included
};

struct Callbacks
{
  //! The drag window is on screen (following the cursor); call pressAt().
  std::function<void()> ready;
  //! The drag window is parked at the pressAt() point: inject the press there.
  std::function<void()> placed;
  //! The drag session ended. accepted = a drop target took the files.
  std::function<void(bool accepted)> ended;
  //! A file was handed to the drop target (or failed to be).
  std::function<void(size_t index, bool ok)> delivered;
  //! The user cancelled the transfer after the drop (Finder's cancel button).
  std::function<void()> cancelRequested;
};

//! Starts the drag helper ahead of time so the first drag is not delayed.
void prewarmReceive();

/*!
Starts the receiving half of a drag: the drag helper process shows a tiny
invisible panel at the global point (x, y) (top-left origin coordinates,
like CGEvent). When the injected mouse-down reaches it, a native drag
session starts with one file promise per item. A promise is fulfilled once
fileReady() was called for it.

All functions may be called from any thread; callbacks run on a helper
reader thread and must only post events.
*/
void beginReceive(uint32_t transferId, std::vector<Item> items, double x, double y, Callbacks callbacks);

/*!
Like beginReceive(), but drags local files as they are (file URLs, as a drag
started in Finder): each Item's stagingPath is the file itself, which the drop
target copies or moves. Nothing is delivered: Callbacks::delivered and
cancelRequested never fire. Used on the server to give a drag back when the
cursor returns from a client before the drop.
*/
void beginLocal(uint32_t dragId, std::vector<Item> items, double x, double y, Callbacks callbacks);

//! Stop following the cursor and park the drag window at (x, y), where the
//! caller is about to press; Callbacks::placed fires once it is there.
void pressAt(uint32_t transferId, double x, double y);

//! The item at \p index is complete at its staging path.
void itemInfo(uint32_t transferId, size_t index, uint64_t size, uint32_t entries);

void itemReady(uint32_t transferId, size_t index);

//! Bytes received so far for an item; shown as Finder progress after the drop.
void itemProgress(uint32_t transferId, size_t index, uint64_t bytes);

//! The transfer failed or was cancelled: fail all pending promises and
//! hide the drag panel if the drag did not start yet.
void abortReceive(uint32_t transferId);

} // namespace deskflow::osx::filedrag
