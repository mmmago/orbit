/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include "base/Event.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

//! Cross-computer file drag and drop (protocol 1.9).
/*!
The computer where the drag starts sends a manifest (kMsgDFileDragBegin)
followed by the file contents in chunks (kMsgDFileDragData). The receiving
computer stores them in a staging folder and hands them to the local drop
target once the user releases the mouse button.

A manifest lists entries (files, folders and symlinks) by relative path.
Entries without a '/' are the dragged items; a folder item is followed by
its contents, parents always before children. Entries keep their permission
bits, and symlinks inside a folder are kept as long as they point inside the
dragged item, so app bundles (executables, framework links) survive.
*/
namespace deskflow::filedrag {

//! Size of one data chunk on the wire.
inline constexpr uint32_t kChunkSize = 64 * 1024;

//! Maximum number of unacknowledged chunks in flight. Keeps file data from
//! delaying mouse and keyboard messages on the shared connection.
inline constexpr uint32_t kWindowChunks = 8;

//! The receiver acknowledges after this many chunks (and at each file end).
inline constexpr uint32_t kAckEveryChunks = 4;

//! Limits accepted in a manifest.
inline constexpr uint32_t kMaxEntries = 200000;
inline constexpr uint32_t kMaxPathLength = 4096;
inline constexpr uint32_t kMaxDepth = 64;

//! Reasons carried by kMsgDFileDragCancel.
enum class CancelReason : uint8_t
{
  Dropped = 0,   ///< Drag ended without a drop (cancelled by the user)
  Error = 1,     ///< Read or write failure
  Refused = 2,   ///< Receiver does not accept file drags or the transfer
  Superseded = 3 ///< A newer drag replaced this one
};

enum class EntryType : uint8_t
{
  File = 0,
  Directory = 1,
  Symlink = 2 ///< Never a dragged item; target is relative and stays inside the item
};

struct Entry
{
  std::string path;  ///< Relative, '/'-separated, e.g. "Photos/2026/a.jpg"
  uint64_t size = 0; ///< 0 for folders and symlinks
  EntryType type = EntryType::File;
  uint16_t mode = 0;  ///< Permission bits (0777 mask); 0 means the default
  std::string target; ///< Symlinks only, e.g. "Versions/Current/Resources"

  bool isDirectory() const
  {
    return type == EntryType::Directory;
  }
  bool isSymlink() const
  {
    return type == EntryType::Symlink;
  }
  //! True for entries that carry data (regular files).
  bool isFile() const
  {
    return type == EntryType::File;
  }
};

//! A dragged item of an incoming drag, as handed to the platform computer.
struct StagedItem
{
  std::string name;
  uint64_t size = 0; ///< Total bytes (all files inside a folder)
  bool isDirectory = false;
  std::string path;        ///< Location in the staging folder once complete
  uint32_t entryCount = 1; ///< Files and folders in this item, itself included
};

struct Manifest
{
  std::vector<Entry> entries;

  uint64_t totalSize() const;
  size_t fileCount() const;

  //! Indices of the dragged items (entries without '/').
  std::vector<size_t> items() const;

  //! Index (into items()) of the dragged item an entry belongs to.
  std::vector<size_t> itemOfEntry() const;

  std::string serialize() const;
  static std::optional<Manifest> parse(const std::string &data);
};

//! True if \p name can be used as a single path component in the staging
//! folder (not empty, not "." or "..", no separators, ':' or NUL, <= 255 bytes).
bool isSafeFileName(const std::string &name);

//! True if every component of the relative path \p path is safe.
bool isSafeRelativePath(const std::string &path);

//! True if a symlink at the relative path \p linkPath may point to \p target:
//! relative, any ".." only at the start, and resolving inside the dragged
//! item (the first component of \p linkPath).
bool isSafeLinkTarget(const std::string &linkPath, const std::string &target);

/*!
Builds a manifest from local paths: regular files, and folders with all
their contents. Symlinks inside folders are kept when isSafeLinkTarget()
accepts them; other symlinks, special files and unreadable entries are
skipped. Fills \p sourcePaths with the local path of each entry. Dragged
items with the same name get " 2", " 3", ... appended.
*/
Manifest buildManifest(const std::vector<std::string> &paths, std::vector<std::string> &sourcePaths);

//! Reads the files of a manifest in order, one chunk at a time.
class FileReader
{
public:
  FileReader(std::vector<std::string> sourcePaths, Manifest manifest);

  struct Chunk
  {
    uint32_t entryIndex = 0;
    std::string data;
    bool fileEnd = false; ///< Last chunk of this file (data may be empty)
  };

  //! Returns the next chunk, or nothing when all files are done.
  //! Throws std::runtime_error when a file cannot be read or changed size.
  std::optional<Chunk> next();

  bool done() const
  {
    return m_index >= m_paths.size();
  }

private:
  void skipDirectories();

  std::vector<std::string> m_paths;
  Manifest m_manifest;
  size_t m_index = 0;
  uint64_t m_offset = 0;
  std::ifstream m_file;
};

//! Writes incoming chunks into a per-transfer staging folder.
class FileWriter
{
public:
  FileWriter(std::filesystem::path stagingRoot, uint32_t transferId, Manifest manifest);
  ~FileWriter();

  FileWriter(const FileWriter &) = delete;
  FileWriter &operator=(const FileWriter &) = delete;

  //! Creates the staging folder and all folders of the manifest.
  bool open();

  //! Appends data to file entry \p index. Returns false on error (bad
  //! index, folder entry, out-of-order data, more data than announced).
  bool write(uint32_t index, const std::string &data);

  //! Marks file entry \p index complete. Returns false if its size does not match.
  bool finish(uint32_t index);

  //! Deletes the staging folder and everything in it.
  void discard();

  const Manifest &manifest() const
  {
    return m_manifest;
  }
  std::filesystem::path entryPath(uint32_t index) const;
  bool isFinished(uint32_t index) const;
  uint32_t chunksReceived() const
  {
    return m_chunks;
  }
  uint64_t bytesWritten(uint32_t index) const
  {
    return index < m_written.size() ? m_written[index] : 0;
  }

private:
  std::filesystem::path m_dir;
  Manifest m_manifest;
  std::vector<uint64_t> m_written;
  std::vector<bool> m_finished;
  std::ofstream m_out;
  uint32_t m_current = UINT32_MAX;
  uint32_t m_chunks = 0;
};

//! Payload of EventTypes::FileDragEnded.
class FileDragEndedInfo : public EventData
{
public:
  FileDragEndedInfo(uint32_t transferId, bool accepted) : m_transferId(transferId), m_accepted(accepted)
  {
  }
  uint32_t m_transferId;
  bool m_accepted;
};

//! Payload of EventTypes::FileDragDelivered.
class FileDragItemInfo : public EventData
{
public:
  FileDragItemInfo(uint32_t transferId, uint32_t index, bool ok) : m_transferId(transferId), m_index(index), m_ok(ok)
  {
  }
  uint32_t m_transferId;
  uint32_t m_index;
  bool m_ok;
};

//! Default staging root, e.g. ~/Library/Caches/Deskflow/Drops on macOS.
std::filesystem::path defaultStagingRoot();

//! Removes stale staging folders left by a previous run.
void clearStagingRoot(const std::filesystem::path &root);

} // namespace deskflow::filedrag
