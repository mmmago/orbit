/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#include "FileDragTests.h"

#include "MockEventQueue.h"
#include "deskflow/Computer.h"
#include "deskflow/FileDrag.h"
#include "deskflow/FileDragReceiver.h"
#include "deskflow/FileDragSender.h"
#include "deskflow/IPlatformComputer.h"
#include <map>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace deskflow::filedrag;

namespace {

class TestDragComputer : public IPlatformComputer
{
public:
  explicit TestDragComputer(IEventQueue *events) : IPlatformComputer(events)
  {
  }
  std::vector<StagedItem> dragItems;
  uint32_t starts = 0, ready = 0, aborted = 0;
  uint64_t size = 0;
  uint32_t entries = 0;
  bool beginFileDrag(uint32_t, const std::vector<StagedItem> &items) override
  {
    dragItems = items;
    ++starts;
    return true;
  }
  void fileDragItemReady(uint32_t, uint32_t) override
  {
    ++ready;
  }
  void abortFileDrag(uint32_t) override
  {
    ++aborted;
  }
  void fileDragItemInfo(uint32_t, uint32_t, uint64_t bytes, uint32_t count) override
  {
    size = bytes;
    entries = count;
  }
  void *getEventTarget() const override
  {
    return const_cast<TestDragComputer *>(this);
  }
  bool getClipboard(ClipboardID id, IClipboard *) const override
  {
    return {};
  }
  void getShape(int32_t &x, int32_t &y, int32_t &width, int32_t &height) const override
  {
  }
  void getCursorPos(int32_t &x, int32_t &y) const override
  {
  }
  void reconfigure(uint32_t activeSides) override
  {
  }
  uint32_t activeSides() override
  {
    return {};
  }
  void warpCursor(int32_t x, int32_t y) override
  {
  }
  uint32_t registerHotKey(KeyID key, KeyModifierMask mask) override
  {
    return {};
  }
  void unregisterHotKey(uint32_t id) override
  {
  }
  void fakeInputBegin() override
  {
  }
  void fakeInputEnd() override
  {
  }
  int32_t getJumpZoneSize() const override
  {
    return {};
  }
  bool isAnyMouseButtonDown(uint32_t &buttonID) const override
  {
    return {};
  }
  void getCursorCenter(int32_t &x, int32_t &y) const override
  {
  }
  void fakeMouseButton(ButtonID id, bool press) override
  {
  }
  void fakeMouseMove(int32_t x, int32_t y) override
  {
  }
  void fakeMouseRelativeMove(int32_t dx, int32_t dy) const override
  {
  }
  void fakeMouseWheel(ScrollDelta delta) const override
  {
  }
  void updateKeyMap() override
  {
  }
  void updateKeyState() override
  {
  }
  void setHalfDuplexMask(KeyModifierMask) override
  {
  }
  void fakeKeyDown(KeyID id, KeyModifierMask mask, KeyButton button, const std::string &lang) override
  {
  }
  bool fakeKeyRepeat(KeyID id, KeyModifierMask mask, int32_t count, KeyButton button, const std::string &lang) override
  {
    return {};
  }
  bool fakeKeyUp(KeyButton button) override
  {
    return {};
  }
  void fakeAllKeysUp() override
  {
  }
  bool fakeCtrlAltDel() override
  {
    return {};
  }
  bool fakeMediaKey(KeyID id) override
  {
    return {};
  }
  bool isKeyDown(KeyButton) const override
  {
    return {};
  }
  KeyModifierMask getActiveModifiers() const override
  {
    return {};
  }
  KeyModifierMask pollActiveModifiers() const override
  {
    return {};
  }
  int32_t pollActiveGroup() const override
  {
    return {};
  }
  void pollPressedKeys(KeyButtonSet &pressedKeys) const override
  {
  }
  void enable() override
  {
  }
  void disable() override
  {
  }
  void enter() override
  {
  }
  bool canLeave() override
  {
    return {};
  }
  void leave() override
  {
  }
  bool setClipboard(ClipboardID id, const IClipboard *) override
  {
    return {};
  }
  void checkClipboards() override
  {
  }
  void openScreensaver(bool notify) override
  {
  }
  void closeScreensaver() override
  {
  }
  void screensaver(bool activate) override
  {
  }
  void resetOptions() override
  {
  }
  void setOptions(const OptionsList &options) override
  {
  }
  void setSequenceNumber(uint32_t) override
  {
  }
  std::string getSecureInputApp() const override
  {
    return {};
  }
  bool isPrimary() const override
  {
    return {};
  }
  void handleSystemEvent(const Event &event) override
  {
  }
};

class DragEvents : public MockEventQueue
{
public:
  std::map<EventTypes, EventHandler> handlers;
  void addHandler(EventTypes type, void *, const EventHandler &handler) override
  {
    handlers[type] = handler;
  }
  void removeHandler(EventTypes type, void *) override
  {
    handlers.erase(type);
  }
  bool dispatchEvent(const Event &event) override
  {
    const auto it = handlers.find(event.getType());
    if (it != handlers.end())
      it->second(event);
    return it != handlers.end();
  }
};

class DragRemote : public FileDragRemote
{
public:
  uint32_t acks = 0, cancels = 0;
  void sendFileDragAck(uint32_t, uint32_t chunks) override
  {
    acks = chunks;
  }
  void sendFileDragCancel(uint32_t, uint8_t) override
  {
    ++cancels;
  }
};

fs::path writeFile(const fs::path &path, const std::string &content)
{
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << content;
  return path;
}

std::string readFile(const fs::path &path)
{
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

Entry file(const std::string &path, uint64_t size)
{
  return {path, size, EntryType::File};
}

Entry dir(const std::string &path)
{
  return {path, 0, EntryType::Directory};
}

Entry link(const std::string &path, const std::string &target)
{
  return {path, 0, EntryType::Symlink, 0, target};
}

fs::perms permsOf(const fs::path &path)
{
  return fs::status(path).permissions() & fs::perms::mask;
}

std::vector<std::string> paths(const Manifest &m)
{
  std::vector<std::string> out;
  for (const auto &e : m.entries)
    out.push_back(e.path);
  return out;
}

} // namespace

void FileDragTests::initTestCase()
{
  QVERIFY(m_tmp.isValid());
}

void FileDragTests::manifestRoundTrip()
{
  Manifest m;
  m.entries = {
      file("a.txt", 12), dir("Photos été"), file("Photos été/big.mov", 5'000'000'000ULL), dir("Photos été/empty"),
      file("empty", 0)
  };
  const auto parsed = Manifest::parse(m.serialize());
  QVERIFY(parsed.has_value());
  QCOMPARE(parsed->entries.size(), size_t(5));
  QCOMPARE(parsed->entries[2].path, std::string("Photos été/big.mov"));
  QCOMPARE(parsed->entries[2].size, 5'000'000'000ULL);
  QVERIFY(parsed->entries[3].isDirectory());
  QCOMPARE(parsed->totalSize(), 5'000'000'012ULL);
  QCOMPARE(parsed->fileCount(), size_t(3));
}

void FileDragTests::manifestRejectsUnsafePaths()
{
  for (const char *bad : {"", ".", "..", "../etc/passwd", "a/../b", "a//b", "/abs", "a/", "a\\b", "x:y"}) {
    Manifest m;
    m.entries = {dir("a"), file(bad, 1)};
    QVERIFY2(!Manifest::parse(m.serialize()).has_value(), bad);
  }
  QVERIFY(!isSafeFileName(std::string(256, 'a')));
  QVERIFY(isSafeFileName(std::string(255, 'a')));
  QVERIFY(!isSafeFileName(std::string("a\0b", 3)));

  std::string deep = "d";
  for (uint32_t i = 1; i < kMaxDepth + 1; ++i)
    deep += "/d";
  QVERIFY(!isSafeRelativePath(deep));

  Manifest dup;
  dup.entries = {file("a", 1), file("a", 1)};
  QVERIFY(!Manifest::parse(dup.serialize()).has_value());

  Manifest sizedDir;
  sizedDir.entries = {{"a", 3, EntryType::Directory}};
  QVERIFY(!Manifest::parse(sizedDir.serialize()).has_value());
}

void FileDragTests::manifestRejectsChildBeforeParent()
{
  Manifest m;
  m.entries = {file("Photos/a.jpg", 1), dir("Photos")};
  QVERIFY(!Manifest::parse(m.serialize()).has_value());

  Manifest fileParent;
  fileParent.entries = {file("Photos", 1), file("Photos/a.jpg", 1)};
  QVERIFY(!Manifest::parse(fileParent.serialize()).has_value());
}

void FileDragTests::manifestRejectsTruncatedOrOldData()
{
  Manifest m;
  m.entries = {file("a.txt", 12)};
  const std::string data = m.serialize();
  QVERIFY(!Manifest::parse(data.substr(0, data.size() - 1)).has_value());
  QVERIFY(!Manifest::parse(data + "x").has_value());

  // version 1 layout (count, size, name) is refused
  std::string v1("\0\0\0\x01", 4);
  v1 += std::string("\0\0\0\0\0\0\0\x0c", 8) + std::string("\0\0\0\x05", 4) + "a.txt";
  QVERIFY(!Manifest::parse(v1).has_value());
}

void FileDragTests::manifestItems()
{
  Manifest m;
  m.entries = {file("a.txt", 1), dir("F"), file("F/x", 1), dir("F/G"), file("F/G/y", 1), file("b.txt", 1)};
  QCOMPARE(m.items(), (std::vector<size_t>{0, 1, 5}));
  QCOMPARE(m.itemOfEntry(), (std::vector<size_t>{0, 1, 1, 1, 1, 2}));
}

void FileDragTests::manifestKeepsModesAndSymlinks()
{
  Manifest m;
  m.entries = {
      dir("A.app"), dir("A.app/Contents"), dir("A.app/Contents/MacOS"), file("A.app/Contents/MacOS/A", 4),
      link("A.app/Contents/Current", "MacOS")
  };
  m.entries[3].mode = 0755;
  const auto parsed = Manifest::parse(m.serialize());
  QVERIFY(parsed.has_value());
  QCOMPARE(parsed->entries[3].mode, uint16_t(0755));
  QVERIFY(parsed->entries[4].isSymlink());
  QCOMPARE(parsed->entries[4].target, std::string("MacOS"));
  QCOMPARE(parsed->fileCount(), size_t(1));
  QCOMPARE(parsed->totalSize(), uint64_t(4));
}

void FileDragTests::manifestRejectsUnsafeLinkTargets()
{
  QVERIFY(isSafeLinkTarget("F/G/l", "../x"));
  QVERIFY(isSafeLinkTarget("F/G/l", "Versions/Current/x"));
  QVERIFY(isSafeLinkTarget("F/l", "."));
  for (const char *bad : {"", "/etc/passwd", "..", "../x", "a/../..", "a/..", "x/../../y"}) {
    QVERIFY2(!isSafeLinkTarget("F/l", bad), bad);
    Manifest m;
    m.entries = {dir("F"), link("F/l", bad)};
    QVERIFY2(!Manifest::parse(m.serialize()).has_value(), bad);
  }
  QVERIFY(!isSafeLinkTarget("F/G/l", "../../x"));

  // a dragged item cannot be a symlink, and a symlink is not a parent
  Manifest top;
  top.entries = {link("l", "x")};
  QVERIFY(!Manifest::parse(top.serialize()).has_value());
  Manifest parent;
  parent.entries = {dir("F"), dir("F/d"), link("F/l", "d"), file("F/l/x", 1)};
  QVERIFY(!Manifest::parse(parent.serialize()).has_value());
}

void FileDragTests::manifestAcceptsVersion2()
{
  // DFM2: u8 type, u64 size, u32 path length, path
  std::string v2("DFM2", 4);
  v2 += std::string("\0\0\0\x01", 4);
  v2 += std::string("\0", 1) + std::string("\0\0\0\0\0\0\0\x0c", 8) + std::string("\0\0\0\x05", 4) + "a.txt";
  const auto parsed = Manifest::parse(v2);
  QVERIFY(parsed.has_value());
  QCOMPARE(parsed->entries[0].path, std::string("a.txt"));
  QCOMPARE(parsed->entries[0].size, uint64_t(12));
  QCOMPARE(parsed->entries[0].mode, uint16_t(0));
}

void FileDragTests::buildManifestWalksFolders()
{
  const fs::path root = fs::path(m_tmp.path().toStdString()) / "walk";
  writeFile(root / "Album" / "b.jpg", "bb");
  writeFile(root / "Album" / "a.jpg", "a");
  writeFile(root / "Album" / "Sub" / "c.txt", "ccc");
  fs::create_directories(root / "Album" / "Empty");
  fs::create_symlink(root / "Album" / "a.jpg", root / "Album" / "link.jpg"); // absolute: skipped
  fs::create_symlink("../../outside", root / "Album" / "Sub" / "escape");    // leaves the item: skipped
  fs::create_symlink("a.jpg", root / "Album" / "rel.jpg");                   // kept
  const auto single = writeFile(root / "single.txt", "single");

  std::vector<std::string> sources;
  const auto m = buildManifest({(root / "Album").string(), single.string()}, sources);
  QCOMPARE(
      paths(m), (std::vector<std::string>{
                    "Album", "Album/Empty", "Album/Sub", "Album/a.jpg", "Album/b.jpg", "Album/rel.jpg",
                    "Album/Sub/c.txt", "single.txt"
                })
  );
  QVERIFY(m.entries[5].isSymlink());
  QCOMPARE(m.entries[5].target, std::string("a.jpg"));
  QCOMPARE(m.totalSize(), uint64_t(1 + 2 + 3 + 6));
  QCOMPARE(sources.size(), m.entries.size());
  QCOMPARE(sources[6], (root / "Album" / "Sub" / "c.txt").string());
  QVERIFY(Manifest::parse(m.serialize()).has_value()); // what we send is accepted
}

void FileDragTests::buildManifestRenamesClashingItems()
{
  const fs::path root = fs::path(m_tmp.path().toStdString()) / "clash";
  const auto a = writeFile(root / "one" / "report.pdf", "1");
  const auto b = writeFile(root / "two" / "report.pdf", "2");
  const auto c = writeFile(root / "three" / "report.pdf", "3");
  std::vector<std::string> sources;
  const auto m = buildManifest({a.string(), b.string(), c.string()}, sources);
  QCOMPARE(paths(m), (std::vector<std::string>{"report.pdf", "report 2.pdf", "report 3.pdf"}));
}

void FileDragTests::readerToWriterRoundTripWithFolders()
{
  const fs::path src = fs::path(m_tmp.path().toStdString()) / "src";
  std::string big(kChunkSize * 3 + 123, '\0');
  for (size_t i = 0; i < big.size(); ++i)
    big[i] = static_cast<char>(i * 31);
  writeFile(src / "Project" / "video.bin", big);
  writeFile(src / "Project" / "notes" / "todo.txt", "todo");
  writeFile(src / "Project" / "empty.txt", "");
  fs::create_directories(src / "Project" / "cache");
  const auto loose = writeFile(src / "loose.txt", "loose");

  std::vector<std::string> sources;
  const auto manifest = buildManifest({(src / "Project").string(), loose.string()}, sources);

  const fs::path staging = fs::path(m_tmp.path().toStdString()) / "staging";
  FileWriter writer(staging, 7, *Manifest::parse(manifest.serialize()));
  QVERIFY(writer.open());

  FileReader reader(sources, manifest);
  int chunks = 0;
  while (auto chunk = reader.next()) {
    QVERIFY(!manifest.entries[chunk->entryIndex].isDirectory());
    if (!chunk->data.empty()) {
      QVERIFY(writer.write(chunk->entryIndex, chunk->data));
      ++chunks;
    }
    if (chunk->fileEnd)
      QVERIFY(writer.finish(chunk->entryIndex));
  }
  QVERIFY(reader.done());
  QCOMPARE(chunks, 4 + 1 + 1); // video (4), todo (1), loose (1); empty.txt has none
  for (uint32_t i = 0; i < manifest.entries.size(); ++i)
    QVERIFY(writer.isFinished(i));

  const fs::path out = staging / "7";
  QVERIFY(readFile(out / "Project" / "video.bin") == big);
  QCOMPARE(readFile(out / "Project" / "notes" / "todo.txt"), std::string("todo"));
  QVERIFY(fs::is_regular_file(out / "Project" / "empty.txt"));
  QVERIFY(fs::is_directory(out / "Project" / "cache"));
  QCOMPARE(readFile(out / "loose.txt"), std::string("loose"));
}

void FileDragTests::readerToWriterRoundTripKeepsAppBundle()
{
  // the shape that broke apps: an executable and framework version links
  const fs::path src = fs::path(m_tmp.path().toStdString()) / "appsrc";
  const fs::path app = src / "Tool.app";
  writeFile(app / "Contents" / "Info.plist", "<plist/>");
  const auto exe = writeFile(app / "Contents" / "MacOS" / "Tool", "\xcf\xfa\xed\xfe");
  fs::permissions(exe, static_cast<fs::perms>(0755));
  const fs::path fw = app / "Contents" / "Frameworks" / "Kit.framework";
  writeFile(fw / "Versions" / "A" / "Kit", "kit");
  fs::permissions(fw / "Versions" / "A" / "Kit", static_cast<fs::perms>(0755));
  fs::create_symlink("A", fw / "Versions" / "Current");
  fs::create_symlink("Versions/Current/Kit", fw / "Kit");

  std::vector<std::string> sources;
  const auto manifest = buildManifest({app.string()}, sources);

  const fs::path staging = fs::path(m_tmp.path().toStdString()) / "appstaging";
  FileWriter writer(staging, 9, *Manifest::parse(manifest.serialize()));
  QVERIFY(writer.open());
  FileReader reader(sources, manifest);
  while (auto chunk = reader.next()) {
    QVERIFY(manifest.entries[chunk->entryIndex].isFile());
    if (!chunk->data.empty())
      QVERIFY(writer.write(chunk->entryIndex, chunk->data));
    if (chunk->fileEnd)
      QVERIFY(writer.finish(chunk->entryIndex));
  }
  for (uint32_t i = 0; i < manifest.entries.size(); ++i)
    QVERIFY(writer.isFinished(i));

  const fs::path out = staging / "9" / "Tool.app";
  QCOMPARE(permsOf(out / "Contents" / "MacOS" / "Tool"), static_cast<fs::perms>(0755));
  QCOMPARE(permsOf(out / "Contents" / "Info.plist"), permsOf(app / "Contents" / "Info.plist"));
  const fs::path outFw = out / "Contents" / "Frameworks" / "Kit.framework";
  QVERIFY(fs::is_symlink(outFw / "Versions" / "Current"));
  QCOMPARE(fs::read_symlink(outFw / "Versions" / "Current").string(), std::string("A"));
  QVERIFY(fs::is_symlink(outFw / "Kit"));
  QCOMPARE(readFile(outFw / "Kit"), std::string("kit")); // resolves through both links
  QCOMPARE(permsOf(outFw / "Kit"), static_cast<fs::perms>(0755));
}

void FileDragTests::writerRejectsTooMuchData()
{
  Manifest m;
  m.entries = {dir("F"), file("F/a.txt", 3)};
  FileWriter writer(fs::path(m_tmp.path().toStdString()) / "toomuch", 1, m);
  QVERIFY(writer.open());
  QVERIFY(!writer.write(1, "abcd"));
  QVERIFY(!writer.write(0, "a")); // folder entry
  QVERIFY(!writer.write(2, "a")); // bad index
}

void FileDragTests::writerRejectsShortFile()
{
  Manifest m;
  m.entries = {file("a.txt", 3), file("b.txt", 1)};
  FileWriter writer(fs::path(m_tmp.path().toStdString()) / "short", 1, m);
  QVERIFY(writer.open());
  QVERIFY(writer.write(0, "ab"));
  QVERIFY(!writer.finish(0));
  QVERIFY(!writer.write(1, "x")); // previous file not finished
}

void FileDragTests::writerDiscardRemovesStaging()
{
  const fs::path root = fs::path(m_tmp.path().toStdString()) / "discard";
  Manifest m;
  m.entries = {file("a.txt", 1)};
  {
    FileWriter writer(root, 3, m);
    QVERIFY(writer.open());
    QVERIFY(writer.write(0, "a"));
    QVERIFY(fs::exists(root / "3"));
  }
  QVERIFY(!fs::exists(root / "3"));
}

void FileDragTests::manifestAssemblerJoinsParts()
{
  using namespace deskflow::filedrag;
  ManifestAssembler parts;
  // a manifest sent whole
  QCOMPARE(parts.finish(1, "whole"), std::string("whole"));
  // in parts
  QVERIFY(parts.addPart(2, "ab"));
  QVERIFY(parts.addPart(2, "cd"));
  QCOMPARE(parts.finish(2, "ef"), std::string("abcdef"));
  // nothing left over for the next one
  QCOMPARE(parts.finish(3, "gh"), std::string("gh"));
  // parts of an abandoned transfer don't leak into the next one
  QVERIFY(parts.addPart(4, "stale"));
  QVERIFY(parts.addPart(5, "fresh-"));
  QCOMPARE(parts.finish(5, "end"), std::string("fresh-end"));
  QVERIFY(parts.addPart(6, "stale"));
  QCOMPARE(parts.finish(7, "other"), std::string("other"));
}

void FileDragTests::manifestAssemblerDropsOversized()
{
  using namespace deskflow::filedrag;
  ManifestAssembler parts;
  const std::string big(kMaxManifestSize / 2 + 1, 'x');
  QVERIFY(parts.addPart(1, big));
  QVERIFY(!parts.addPart(1, big));
  QVERIFY(!parts.addPart(1, "more"));
  // empty: fails to parse, so the drag is refused instead of truncated
  QVERIFY(parts.finish(1, "end").empty());
  QVERIFY(!Manifest::parse(""));
  // and the next transfer starts clean
  QCOMPARE(parts.finish(2, "next"), std::string("next"));
}

namespace {
// one ack, as the receiver sends after kAckEveryChunks full chunks
constexpr uint32_t kAckBytes = deskflow::filedrag::kAckEveryChunks * deskflow::filedrag::kChunkSize;

// acks one full round of \p control's window, every ack \p delay seconds
void ackRound(deskflow::filedrag::WindowControl &control, double delay, bool limited)
{
  const uint32_t window = control.window();
  for (uint32_t acked = 0; acked < window; acked += kAckBytes)
    control.acked(kAckBytes, delay, limited);
}
} // namespace

void FileDragTests::windowGrowsWhileDelayStaysLow()
{
  using namespace deskflow::filedrag;
  WindowControl control;
  QCOMPARE(control.window(), kWindowBytes);
  ackRound(control, 0.004, true); // sets the base delay
  QCOMPARE(control.window(), kWindowBytes + kWindowStepBytes);
  // Wi-Fi jitter: only the round's best ack counts
  const uint32_t window = control.window();
  for (uint32_t acked = 0; acked < window; acked += kAckBytes)
    control.acked(kAckBytes, acked == 0 ? 0.006 : 0.030, true);
  QCOMPARE(control.window(), window + kWindowStepBytes);
}

void FileDragTests::windowHoldsWhenNotLimited()
{
  using namespace deskflow::filedrag;
  WindowControl control;
  // the reader (disk, privacy prompt) is the limit, not the window
  ackRound(control, 0.004, false);
  ackRound(control, 0.004, false);
  QCOMPARE(control.window(), kWindowBytes);
  // between the thresholds: hold
  ackRound(control, 0.004 + (WindowControl::kGrowBelow + WindowControl::kShrinkAbove) / 2, true);
  QCOMPARE(control.window(), kWindowBytes);
}

void FileDragTests::windowShrinksWhenDelayRises()
{
  using namespace deskflow::filedrag;
  WindowControl control;
  for (int i = 0; i < 10; ++i)
    ackRound(control, 0.004, true);
  const uint32_t grown = control.window();
  QCOMPARE(grown, kWindowBytes + 10 * kWindowStepBytes);
  ackRound(control, 0.004 + WindowControl::kShrinkAbove * 2, true);
  QCOMPARE(control.window(), grown * 3 / 4);

  control.reset();
  QCOMPARE(control.window(), kWindowBytes);
  QVERIFY(control.baseDelay() < 0);
}

void FileDragTests::windowStaysWithinBounds()
{
  using namespace deskflow::filedrag;
  WindowControl control;
  for (int i = 0; i < 1000; ++i)
    ackRound(control, 0.001, true);
  QCOMPARE(control.window(), kMaxWindowBytes);
  for (int i = 0; i < 100; ++i)
    ackRound(control, 0.5, true);
  QCOMPARE(control.window(), kMinWindowBytes);
  // acks without a timing sample (stale or out-of-order) change nothing
  ackRound(control, -1, true);
  QCOMPARE(control.window(), kMinWindowBytes);
}

void FileDragTests::previewCanBeDroppedBeforeManifest()
{
  DragEvents events;
  auto *platform = new TestDragComputer(&events);
  deskflow::Computer computer(platform, &events);
  FileDragReceiver receiver(&events, &computer, m_tmp.filePath("Drops").toStdString());
  DragRemote remote;
  Manifest preview{{dir("Album")}};
  Manifest full{{dir("Album"), file("Album/a.txt", 3)}};
  QVERIFY(receiver.begin(101, preview.serialize(), &remote, true));
  receiver.startDrag();
  QCOMPARE(platform->starts, 1u);
  QCOMPARE(platform->ready, 0u); // a preview must never deliver an empty folder
  events.dispatchEvent(Event(EventTypes::FileDragEnded, computer.getEventTarget(), new FileDragEndedInfo(101, true)));
  QVERIFY(!receiver.abortUndropped()); // accepted drop survives moving away
  QVERIFY(receiver.completeManifest(101, full.serialize(), &remote));
  QCOMPARE(platform->starts, 1u); // keep the existing promises, even after mouse-up
  QCOMPARE(platform->size, uint64_t(3));
  QCOMPARE(platform->entries, 2u);
  QCOMPARE(platform->ready, 0u);
  receiver.data(101, 1, "abc");
  receiver.fileEnd(101, 1);
  QCOMPARE(platform->ready, 1u);
  QCOMPARE(remote.acks, 1u);
  QCOMPARE(remote.cancels, 0u);
  QCOMPARE(readFile(platform->dragItems.front().path + "/a.txt"), std::string("abc"));
}

void FileDragTests::cancelledPreviewStaysCancelled()
{
  DragEvents events;
  auto *platform = new TestDragComputer(&events);
  deskflow::Computer computer(platform, &events);
  FileDragReceiver receiver(&events, &computer, m_tmp.filePath("Drops").toStdString());
  DragRemote remote;
  Manifest preview{{dir("Album")}};
  QVERIFY(receiver.begin(102, preview.serialize(), &remote, true));
  receiver.startDrag();
  QVERIFY(receiver.abortUndropped());
  QCOMPARE(receiver.transfers(), size_t(0));
  QVERIFY(!receiver.completeManifest(102, preview.serialize(), &remote));
  QCOMPARE(platform->starts, 1u);
  QCOMPARE(platform->ready, 0u);
  QCOMPARE(platform->aborted, 1u);
  QCOMPARE(receiver.transfers(), size_t(0));
}

void FileDragTests::previewRejectsChangedItems()
{
  DragEvents events;
  auto *platform = new TestDragComputer(&events);
  deskflow::Computer computer(platform, &events);
  FileDragReceiver receiver(&events, &computer, m_tmp.filePath("Drops").toStdString());
  DragRemote remote;
  Manifest preview{{dir("Album")}};
  Manifest changed{{dir("Other")}};
  QVERIFY(receiver.begin(103, preview.serialize(), &remote, true));
  receiver.startDrag();
  QVERIFY(!receiver.completeManifest(103, changed.serialize(), &remote));
  QCOMPARE(remote.cancels, 1u);
  QCOMPARE(platform->ready, 0u);
  QCOMPARE(receiver.transfers(), size_t(0));
}

void FileDragTests::previewCompletesEmptyFolder()
{
  DragEvents events;
  auto *platform = new TestDragComputer(&events);
  deskflow::Computer computer(platform, &events);
  FileDragReceiver receiver(&events, &computer, m_tmp.filePath("Drops").toStdString());
  DragRemote remote;
  Manifest preview{{dir("Empty")}};
  QVERIFY(receiver.begin(104, preview.serialize(), &remote, true));
  // Full list may arrive before the platform starts the pending drag.
  QVERIFY(receiver.completeManifest(104, preview.serialize(), &remote));
  QCOMPARE(platform->ready, 0u);
  receiver.startDrag();
  QCOMPARE(platform->ready, 1u);
  QCOMPARE(platform->starts, 1u);
}

QTEST_MAIN(FileDragTests)
