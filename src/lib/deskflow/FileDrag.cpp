/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/FileDrag.h"

#include "common/Constants.h"

#include "base/Log.h"

#include <QStandardPaths>

#include <algorithm>
#include <set>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

namespace deskflow::filedrag {

namespace {

// Manifest layout: u32 magic, u32 count, then per entry:
// u8 type, u16 mode, u64 size, u32 path length, path bytes, and for symlinks
// u32 target length, target bytes. 'DFM2' (still accepted) has no mode and
// no symlinks. The magics are larger than any v1 file count, so a v1
// receiver refuses them; a DFM2 receiver refuses DFM3.
constexpr uint32_t kManifestMagicV2 = 0x44464D32; // 'DFM2'
constexpr uint32_t kManifestMagic = 0x44464D33;   // 'DFM3'

constexpr uint16_t kModeMask = 0777;

void putU32(std::string &out, uint32_t v)
{
  for (int shift = 24; shift >= 0; shift -= 8)
    out.push_back(static_cast<char>((v >> shift) & 0xff));
}

void putU64(std::string &out, uint64_t v)
{
  for (int shift = 56; shift >= 0; shift -= 8)
    out.push_back(static_cast<char>((v >> shift) & 0xff));
}

void putU16(std::string &out, uint16_t v)
{
  out.push_back(static_cast<char>((v >> 8) & 0xff));
  out.push_back(static_cast<char>(v & 0xff));
}

bool getU8(const std::string &in, size_t &pos, uint8_t &v)
{
  if (pos + 1 > in.size())
    return false;
  v = static_cast<uint8_t>(in[pos++]);
  return true;
}

bool getU16(const std::string &in, size_t &pos, uint16_t &v)
{
  if (pos + 2 > in.size())
    return false;
  v = static_cast<uint16_t>((static_cast<uint8_t>(in[pos]) << 8) | static_cast<uint8_t>(in[pos + 1]));
  pos += 2;
  return true;
}

bool getU32(const std::string &in, size_t &pos, uint32_t &v)
{
  if (pos + 4 > in.size())
    return false;
  v = 0;
  for (int i = 0; i < 4; ++i)
    v = (v << 8) | static_cast<uint8_t>(in[pos++]);
  return true;
}

bool getU64(const std::string &in, size_t &pos, uint64_t &v)
{
  if (pos + 8 > in.size())
    return false;
  v = 0;
  for (int i = 0; i < 8; ++i)
    v = (v << 8) | static_cast<uint8_t>(in[pos++]);
  return true;
}

std::string parentOf(const std::string &path)
{
  const auto slash = path.rfind('/');
  return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string topOf(const std::string &path)
{
  return path.substr(0, path.find('/'));
}

std::string uniqueName(const std::string &name, std::set<std::string> &used)
{
  if (used.insert(name).second)
    return name;
  const auto dot = name.rfind('.');
  const bool hasExt = dot != std::string::npos && dot > 0;
  const std::string stem = hasExt ? name.substr(0, dot) : name;
  const std::string ext = hasExt ? name.substr(dot) : std::string();
  for (int n = 2;; ++n) {
    std::string candidate = stem + " " + std::to_string(n) + ext;
    if (used.insert(candidate).second)
      return candidate;
  }
}

} // namespace

//
// Manifest
//

uint64_t Manifest::totalSize() const
{
  uint64_t total = 0;
  for (const auto &e : entries)
    total += e.size;
  return total;
}

size_t Manifest::fileCount() const
{
  return std::count_if(entries.begin(), entries.end(), [](const Entry &e) { return e.isFile(); });
}

std::vector<size_t> Manifest::items() const
{
  std::vector<size_t> result;
  for (size_t i = 0; i < entries.size(); ++i) {
    if (entries[i].path.find('/') == std::string::npos)
      result.push_back(i);
  }
  return result;
}

std::vector<size_t> Manifest::itemOfEntry() const
{
  std::vector<size_t> result(entries.size(), 0);
  size_t item = 0;
  bool seenItem = false;
  for (size_t i = 0; i < entries.size(); ++i) {
    if (entries[i].path.find('/') == std::string::npos) {
      if (seenItem)
        ++item;
      seenItem = true;
    }
    result[i] = item;
  }
  return result;
}

std::string Manifest::serialize() const
{
  std::string out;
  putU32(out, kManifestMagic);
  putU32(out, static_cast<uint32_t>(entries.size()));
  for (const auto &e : entries) {
    out.push_back(static_cast<char>(e.type));
    putU16(out, e.mode & kModeMask);
    putU64(out, e.size);
    putU32(out, static_cast<uint32_t>(e.path.size()));
    out += e.path;
    if (e.isSymlink()) {
      putU32(out, static_cast<uint32_t>(e.target.size()));
      out += e.target;
    }
  }
  return out;
}

std::optional<Manifest> Manifest::parse(const std::string &data)
{
  size_t pos = 0;
  uint32_t magic = 0;
  uint32_t count = 0;
  if (!getU32(data, pos, magic) || (magic != kManifestMagic && magic != kManifestMagicV2))
    return std::nullopt;
  const bool v3 = magic == kManifestMagic;
  if (!getU32(data, pos, count) || count == 0 || count > kMaxEntries)
    return std::nullopt;

  Manifest m;
  m.entries.reserve(count);
  std::set<std::string> seen;
  std::set<std::string> directories;
  for (uint32_t i = 0; i < count; ++i) {
    Entry e;
    uint8_t type = 0;
    uint32_t len = 0;
    if (!getU8(data, pos, type) || type > (v3 ? 2 : 1) || (v3 && !getU16(data, pos, e.mode)) ||
        !getU64(data, pos, e.size) || !getU32(data, pos, len) || len > kMaxPathLength || pos + len > data.size())
      return std::nullopt;
    e.type = static_cast<EntryType>(type);
    e.mode &= kModeMask;
    e.path.assign(data, pos, len);
    pos += len;
    if (e.isSymlink()) {
      if (!getU32(data, pos, len) || len > kMaxPathLength || pos + len > data.size())
        return std::nullopt;
      e.target.assign(data, pos, len);
      pos += len;
      if (!isSafeLinkTarget(e.path, e.target))
        return std::nullopt;
    }

    if (!isSafeRelativePath(e.path) || !seen.insert(e.path).second)
      return std::nullopt;
    if (!e.isFile() && e.size != 0)
      return std::nullopt;
    // parents come first, and are folders
    if (const auto parent = parentOf(e.path); !parent.empty() && directories.count(parent) == 0)
      return std::nullopt;
    if (e.isDirectory())
      directories.insert(e.path);
    m.entries.push_back(std::move(e));
  }
  if (pos != data.size())
    return std::nullopt;
  return m;
}

bool isSafeFileName(const std::string &name)
{
  if (name.empty() || name.size() > 255 || name == "." || name == "..")
    return false;
  for (char c : name) {
    if (c == '/' || c == '\\' || c == '\0' || c == ':')
      return false;
  }
  return true;
}

bool isSafeRelativePath(const std::string &path)
{
  if (path.empty() || path.size() > kMaxPathLength)
    return false;
  uint32_t depth = 0;
  size_t start = 0;
  while (true) {
    const auto slash = path.find('/', start);
    const auto component = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    if (!isSafeFileName(component) || ++depth > kMaxDepth)
      return false;
    if (slash == std::string::npos)
      return true;
    start = slash + 1;
  }
}

bool isSafeLinkTarget(const std::string &linkPath, const std::string &target)
{
  if (target.empty() || target.size() > kMaxPathLength || target.front() == '/' ||
      target.find('\0') != std::string::npos || linkPath.find('/') == std::string::npos)
    return false;

  // ".." only at the start: the folders it climbs are real folders of the
  // item, so a lexical check matches what the file system resolves.
  size_t up = 0;
  size_t start = 0;
  bool climbing = true;
  while (start <= target.size()) {
    const auto slash = target.find('/', start);
    const auto component = target.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    if (component == "..") {
      if (!climbing)
        return false;
      ++up;
    } else if (!component.empty() && component != ".") {
      climbing = false;
    }
    if (slash == std::string::npos)
      break;
    start = slash + 1;
  }

  // the link's folder, as a depth below the staging folder (the item is 1)
  const auto depth = static_cast<size_t>(std::count(linkPath.begin(), linkPath.end(), '/'));
  return up < depth;
}

Manifest buildManifest(const std::vector<std::string> &paths, std::vector<std::string> &sourcePaths)
{
  Manifest m;
  sourcePaths.clear();
  std::set<std::string> usedNames;

  auto add = [&](std::string rel, const fs::path &source, EntryType type, uint64_t size,
                 const fs::file_status &status, std::string target = {}) {
    const auto mode = static_cast<uint16_t>(static_cast<unsigned>(status.permissions() & fs::perms::mask) & kModeMask);
    m.entries.push_back({std::move(rel), size, type, type == EntryType::Symlink ? uint16_t(0) : mode, std::move(target)});
    sourcePaths.push_back(source.string());
  };

  for (const auto &p : paths) {
    std::error_code ec;
    const auto status = fs::symlink_status(p, ec);
    const auto name = fs::path(p).filename().string();
    if (ec || !isSafeFileName(name) || (!fs::is_regular_file(status) && !fs::is_directory(status))) {
      LOG_INFO("file drag: skipping \"%s\"", p.c_str());
      continue;
    }
    const std::string top = uniqueName(name, usedNames);

    if (fs::is_regular_file(status)) {
      const auto size = fs::file_size(p, ec);
      if (!ec)
        add(top, p, EntryType::File, size, status);
      continue;
    }

    // folder: walk it, parents before children, in a stable order
    add(top, p, EntryType::Directory, 0, status);
    std::vector<fs::path> pending = {fs::path(p)};
    std::vector<std::string> pendingRel = {top};
    while (!pending.empty() && m.entries.size() < kMaxEntries) {
      const fs::path dir = pending.back();
      const std::string dirRel = pendingRel.back();
      pending.pop_back();
      pendingRel.pop_back();

      std::vector<fs::directory_entry> children;
      for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
           it.increment(ec)) {
        children.push_back(*it);
      }
      if (ec) {
        LOG_INFO("file drag: cannot read folder \"%s\": %s", dir.string().c_str(), ec.message().c_str());
        ec.clear();
      }
      std::sort(children.begin(), children.end(), [](const auto &a, const auto &b) { return a.path() < b.path(); });

      std::vector<std::pair<fs::path, std::string>> subdirs;
      for (const auto &child : children) {
        const auto childName = child.path().filename().string();
        const std::string rel = dirRel + "/" + childName;
        const auto childStatus = child.symlink_status(ec);
        if (ec || !isSafeRelativePath(rel)) {
          ec.clear();
          continue;
        }
        if (fs::is_regular_file(childStatus)) {
          const auto size = child.file_size(ec);
          if (!ec)
            add(rel, child.path(), EntryType::File, size, childStatus);
          ec.clear();
        } else if (fs::is_directory(childStatus)) {
          add(rel, child.path(), EntryType::Directory, 0, childStatus);
          subdirs.emplace_back(child.path(), rel);
        } else if (fs::is_symlink(childStatus)) {
          // relative links inside the item (e.g. framework "Versions/Current")
          auto target = fs::read_symlink(child.path(), ec).string();
          if (!ec && isSafeLinkTarget(rel, target))
            add(rel, child.path(), EntryType::Symlink, 0, childStatus, std::move(target));
          else
            LOG_DEBUG("file drag: skipping symlink \"%s\"", child.path().string().c_str());
          ec.clear();
        }
        // special files are skipped
      }
      // depth first, keeping the sorted order
      for (auto it = subdirs.rbegin(); it != subdirs.rend(); ++it) {
        pending.push_back(it->first);
        pendingRel.push_back(it->second);
      }
    }
    if (m.entries.size() >= kMaxEntries) {
      LOG_WARN("file drag: too many files, the drag is truncated at %u entries", kMaxEntries);
      break;
    }
  }
  return m;
}

//
// FileReader
//

FileReader::FileReader(std::vector<std::string> sourcePaths, Manifest manifest)
    : m_paths(std::move(sourcePaths)),
      m_manifest(std::move(manifest))
{
  skipDirectories();
}

void FileReader::skipDirectories()
{
  while (m_index < m_paths.size() && !m_manifest.entries[m_index].isFile())
    ++m_index;
}

std::optional<FileReader::Chunk> FileReader::next()
{
  if (done())
    return std::nullopt;

  const auto index = static_cast<uint32_t>(m_index);
  const uint64_t expected = m_manifest.entries[m_index].size;

  if (!m_file.is_open()) {
    m_file.open(m_paths[m_index], std::ios::binary);
    if (!m_file)
      throw std::runtime_error("cannot open " + m_paths[m_index]);
    m_offset = 0;
  }

  Chunk chunk;
  chunk.entryIndex = index;
  const uint64_t remaining = expected - m_offset;
  const auto want = static_cast<size_t>(std::min<uint64_t>(remaining, kChunkSize));
  chunk.data.resize(want);
  if (want > 0) {
    m_file.read(chunk.data.data(), static_cast<std::streamsize>(want));
    if (static_cast<size_t>(m_file.gcount()) != want)
      throw std::runtime_error("file shrank while sending: " + m_paths[m_index]);
  }
  m_offset += want;

  if (m_offset == expected) {
    chunk.fileEnd = true;
    m_file.close();
    ++m_index;
    skipDirectories();
  }
  return chunk;
}

//
// FileWriter
//

FileWriter::FileWriter(fs::path stagingRoot, uint32_t transferId, Manifest manifest)
    : m_dir(std::move(stagingRoot) / std::to_string(transferId)),
      m_manifest(std::move(manifest)),
      m_written(m_manifest.entries.size(), 0),
      m_finished(m_manifest.entries.size(), false)
{
  for (size_t i = 0; i < m_manifest.entries.size(); ++i) {
    if (!m_manifest.entries[i].isFile())
      m_finished[i] = true;
  }
}

FileWriter::~FileWriter()
{
  discard();
}

bool FileWriter::open()
{
  std::error_code ec;
  fs::remove_all(m_dir, ec);
  fs::create_directories(m_dir, ec);
  for (const auto &e : m_manifest.entries) {
    if (ec)
      break;
    if (e.isDirectory()) {
      fs::create_directories(m_dir / e.path, ec);
      // keep the folder writable for its contents
      if (!ec && e.mode != 0)
        fs::permissions(m_dir / e.path, static_cast<fs::perms>(e.mode | 0700), ec);
    } else if (e.isSymlink()) {
      fs::create_symlink(e.target, m_dir / e.path, ec);
    }
  }
  if (ec) {
    LOG_ERR("file drag: cannot create staging folder %s: %s", m_dir.string().c_str(), ec.message().c_str());
    return false;
  }
  return true;
}

fs::path FileWriter::entryPath(uint32_t index) const
{
  return m_dir / m_manifest.entries.at(index).path;
}

bool FileWriter::isFinished(uint32_t index) const
{
  return index < m_finished.size() && m_finished[index];
}

bool FileWriter::write(uint32_t index, const std::string &data)
{
  if (index >= m_manifest.entries.size() || !m_manifest.entries[index].isFile() || m_finished[index])
    return false;
  if (m_written[index] + data.size() > m_manifest.entries[index].size)
    return false;

  if (m_current != index) {
    // files arrive strictly in order; a previous one must be finished
    if (m_current != UINT32_MAX && !m_finished[m_current])
      return false;
    m_out.close();
    m_out.open(entryPath(index).string() + ".part", std::ios::binary | std::ios::trunc);
    if (!m_out)
      return false;
    m_current = index;
  }

  m_out.write(data.data(), static_cast<std::streamsize>(data.size()));
  if (!m_out)
    return false;
  m_written[index] += data.size();
  ++m_chunks;
  return true;
}

bool FileWriter::finish(uint32_t index)
{
  if (index >= m_manifest.entries.size() || !m_manifest.entries[index].isFile() || m_finished[index])
    return false;
  if (m_written[index] != m_manifest.entries[index].size)
    return false;

  if (m_current == index) {
    m_out.close();
  } else {
    // empty file: nothing was written yet
    std::ofstream(entryPath(index).string() + ".part", std::ios::binary | std::ios::trunc);
  }

  std::error_code ec;
  if (const auto mode = m_manifest.entries[index].mode; mode != 0)
    fs::permissions(entryPath(index).string() + ".part", static_cast<fs::perms>(mode | 0600), ec);
  if (!ec)
    fs::rename(entryPath(index).string() + ".part", entryPath(index), ec);
  if (ec)
    return false;
  m_finished[index] = true;
  m_current = UINT32_MAX;
  return true;
}

void FileWriter::discard()
{
  m_out.close();
  std::error_code ec;
  fs::remove_all(m_dir, ec);
}

fs::path defaultStagingRoot()
{
  const auto cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation).toStdString();
  return fs::path(cache) / kAppName / "Drops";
}

void clearStagingRoot(const fs::path &root)
{
  std::error_code ec;
  if (fs::exists(root, ec)) {
    fs::remove_all(root, ec);
  }
}

} // namespace deskflow::filedrag
