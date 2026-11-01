/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClientProxyTests.h"

#include "../deskflow/MockEventQueue.h"
#include "deskflow/AppUtil.h"
#include "io/IStream.h"
#include "server/ClientProxy1_0.h"
#include "server/ClientProxy1_1.h"
#include "server/ClientProxy1_6.h"
#include "server/ClientProxy1_7.h"
#include "server/ClientProxy1_8.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "server/ClientProxy1_10.h"
#include "server/ClientProxy1_9.h"

#include <memory>
#include <string>
#include <vector>

#include <QByteArray>
#include <QTemporaryDir>
#include <QTest>
#include <filesystem>
#include <fstream>

namespace {

class TestAppUtil : public AppUtil
{
public:
  int run() override
  {
    return 0;
  }

  std::vector<std::string> getKeyboardLayoutList() override
  {
    return {"en"};
  }

  std::string getCurrentLanguageCode() override
  {
    return "en";
  }
};

class CapturingStream : public deskflow::IStream
{
public:
  QByteArray take()
  {
    auto bytes = m_buffer;
    m_buffer.clear();
    m_codes.clear();
    m_messages.clear();
    return bytes;
  }

  // message codes written since the last take (one message per write)
  std::vector<std::string> takeCodes()
  {
    auto codes = m_codes;
    take();
    return codes;
  }

  // whole messages written since the last take
  std::vector<QByteArray> takeMessages()
  {
    auto messages = m_messages;
    take();
    return messages;
  }

  void write(const void *buffer, uint32_t n) override
  {
    m_buffer.append(static_cast<const char *>(buffer), n);
    if (n >= 4) {
      m_codes.emplace_back(static_cast<const char *>(buffer), 4);
      m_messages.emplace_back(static_cast<const char *>(buffer), static_cast<int>(n));
    }
  }

  void close() override
  {
  }

  // bytes the proxy will read back (for messages coming from the client)
  void feed(const QByteArray &bytes)
  {
    m_input.append(bytes);
  }

  uint32_t read(void *buffer, uint32_t n) override
  {
    const auto count = std::min<uint32_t>(n, static_cast<uint32_t>(m_input.size()));
    if (buffer != nullptr) {
      memcpy(buffer, m_input.constData(), count);
    }
    m_input.remove(0, count);
    return count;
  }

  void flush() override
  {
  }

  void shutdownInput() override
  {
  }

  void shutdownOutput() override
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<CapturingStream *>(this);
  }

  bool isReady() const override
  {
    return false;
  }

  uint32_t getSize() const override
  {
    return 0;
  }

private:
  QByteArray m_buffer;
  QByteArray m_input;
  std::vector<std::string> m_codes;
  std::vector<QByteArray> m_messages;
};

// exposes parseMessage so tests can deliver client messages
class TestProxy1_9 : public ClientProxy1_9
{
public:
  using ClientProxy1_9::ClientProxy1_9;
  using ClientProxy1_9::parseMessage;
  using ClientProxy1_9::pump;
};

// files are read on a worker thread; pump until \p code was sent \p count
// times or nothing more arrives (the mock event queue drops wake-ups)
std::vector<std::string> pumpUntil(TestProxy1_9 &proxy, CapturingStream *stream, const char *code, long count)
{
  std::vector<std::string> all;
  for (int i = 0; i < 200; ++i) {
    proxy.pump();
    auto codes = stream->takeCodes();
    all.insert(all.end(), codes.begin(), codes.end());
    if (std::count(all.begin(), all.end(), std::string(code, 4)) >= count) {
      // give a stray extra message the chance to show up
      QTest::qWait(20);
      proxy.pump();
      codes = stream->takeCodes();
      all.insert(all.end(), codes.begin(), codes.end());
      break;
    }
    QTest::qWait(5);
  }
  return all;
}

uint32_t readBe32(const QByteArray &bytes, int pos)
{
  const auto *p = reinterpret_cast<const uint8_t *>(bytes.constData() + pos);
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

QByteArray be32(uint32_t v)
{
  QByteArray b;
  for (int shift = 24; shift >= 0; shift -= 8)
    b.append(static_cast<char>((v >> shift) & 0xff));
  return b;
}

long countCode(const std::vector<std::string> &codes, const char *code)
{
  return std::count(codes.begin(), codes.end(), std::string(code, 4));
}

std::unique_ptr<ClientProxy> makeProxy(int minor, deskflow::IStream *stream, IEventQueue *events)
{
  // the 1.4 and later constructors assert the server pointer is non-null but only store it
  auto *server = reinterpret_cast<Server *>(0x1);

  std::unique_ptr<ClientProxy> proxy;
  switch (minor) {
  case 0:
    proxy = std::make_unique<ClientProxy1_0>("client", stream, events);
    break;
  case 1:
    proxy = std::make_unique<ClientProxy1_1>("client", stream, events);
    break;
  case 6:
    proxy = std::make_unique<ClientProxy1_6>("client", stream, server, events);
    break;
  case 7:
    proxy = std::make_unique<ClientProxy1_7>("client", stream, server, events);
    break;
  case 8:
    proxy = std::make_unique<ClientProxy1_8>("client", stream, server, events);
    break;
  default:
    break;
  }
  return proxy;
}

struct ProxyUnderTest
{
  MockEventQueue events;
  CapturingStream *stream = new CapturingStream;
  std::unique_ptr<ClientProxy> proxy;

  explicit ProxyUnderTest(int minor) : proxy(makeProxy(minor, stream, &events))
  {
    // drop the query-info and layout-sync messages the constructors send
    stream->take();
  }
};

const KeyID kKey = 0x61;
const KeyModifierMask kMask = 0;
const KeyButton kButton = 0x1e;
const int32_t kCount = 3;
const std::string kLang = "en";

} // namespace

void ClientProxyTests::initTestCase()
{
  // the 1.8 constructor reads the keyboard layouts through AppUtil::instance()
  static TestAppUtil appUtil;
}

// These formats are frozen because shipped third-party clients parse them byte
// for byte: Synergy 1.4 through 1.14.1 negotiate 1.4 through 1.7, Barrier and
// Input Leap negotiate 1.6, and Synergy 1.14.2 onwards and Deskflow negotiate 1.8.
void ClientProxyTests::keyDown_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKDN" + QByteArray::fromHex("0061 0000");
  QTest::newRow("1.1") << 1 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.6") << 6 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.7") << 7 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.8") << 8 << "DKDL" + QByteArray::fromHex("0061 0000 001e 00000002") + "en";
}

void ClientProxyTests::keyDown()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyDown(kKey, kMask, kButton, kLang);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::keyRepeat_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKRP" + QByteArray::fromHex("0061 0000 0003");
  QTest::newRow("1.1") << 1 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.6") << 6 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.7") << 7 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.8") << 8 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e 00000002") + "en";
}

void ClientProxyTests::keyRepeat()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyRepeat(kKey, kMask, kCount, kButton, kLang);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::keyUp_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKUP" + QByteArray::fromHex("0061 0000");
  QTest::newRow("1.1") << 1 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.6") << 6 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.7") << 7 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.8") << 8 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
}

void ClientProxyTests::keyUp()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyUp(kKey, kMask, kButton);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::fileDragRespectsWindowAndAcks()
{
  using namespace deskflow::filedrag;
  QTemporaryDir dir;
  const std::string path = dir.filePath("data.bin").toStdString();
  const uint32_t chunks = kWindowChunks * 2 + 3;
  std::ofstream(path, std::ios::binary) << std::string(kChunkSize * chunks, 'x');

  MockEventQueue events;
  auto *stream = new CapturingStream;
  TestProxy1_9 proxy("client", stream, reinterpret_cast<Server *>(0x1), &events);
  stream->take();

  QVERIFY(proxy.supportsFileDrag());
  QVERIFY(proxy.startFileDrag({path}));
  // the manifest is built in the background and always precedes the data
  auto codes = pumpUntil(proxy, stream, "DGDT", kWindowChunks);
  QVERIFY(!codes.empty());
  QCOMPARE(codes.front(), std::string("DGBN"));
  QCOMPARE(countCode(codes, "DGBN"), 1L);
  QCOMPARE(countCode(codes, "DGDT"), long(kWindowChunks)); // stops at the window

  // acknowledging half the window lets the same number of chunks through
  stream->feed(be32(1) + be32(kWindowChunks / 2));
  QVERIFY(proxy.parseMessage(reinterpret_cast<const uint8_t *>("DGAK")));
  codes = stream->takeCodes();
  QCOMPARE(countCode(codes, "DGDT"), long(kWindowChunks / 2));

  // acknowledging everything sent so far drains the rest and ends the file
  uint32_t sent = kWindowChunks + kWindowChunks / 2;
  long fileEnds = 0;
  for (int i = 0; i < 50 && sent < chunks; ++i) {
    stream->feed(be32(1) + be32(sent));
    QVERIFY(proxy.parseMessage(reinterpret_cast<const uint8_t *>("DGAK")));
    codes = pumpUntil(proxy, stream, "DGDT", 1);
    sent += static_cast<uint32_t>(countCode(codes, "DGDT"));
    fileEnds += countCode(codes, "DGFE");
  }
  QCOMPARE(sent, chunks);
  QCOMPARE(fileEnds, 1L);
}

void ClientProxyTests::fileDragStopsOnCancel()
{
  using namespace deskflow::filedrag;
  QTemporaryDir dir;
  const std::string path = dir.filePath("data.bin").toStdString();
  std::ofstream(path, std::ios::binary) << std::string(kChunkSize * (kWindowChunks + 4), 'x');

  MockEventQueue events;
  auto *stream = new CapturingStream;
  TestProxy1_9 proxy("client", stream, reinterpret_cast<Server *>(0x1), &events);
  stream->take();

  QVERIFY(proxy.startFileDrag({path}));
  pumpUntil(proxy, stream, "DGDT", kWindowChunks);

  // client: drag ended without a drop
  stream->feed(be32(1));
  stream->feed(QByteArray(1, static_cast<char>(CancelReason::Dropped)));
  QVERIFY(proxy.parseMessage(reinterpret_cast<const uint8_t *>("DGCN")));

  // later acks must not restart the transfer
  stream->feed(be32(1) + be32(kWindowChunks));
  QVERIFY(proxy.parseMessage(reinterpret_cast<const uint8_t *>("DGAK")));
  QCOMPARE(countCode(pumpUntil(proxy, stream, "DGDT", 1), "DGDT"), 0L);

  // nothing to drag
  QVERIFY(!proxy.startFileDrag({}));
}

void ClientProxyTests::fileDragSendsFolders()
{
  using namespace deskflow::filedrag;
  QTemporaryDir dir;
  const std::filesystem::path root = dir.filePath("Album").toStdString();
  std::filesystem::create_directories(root / "Sub");
  std::ofstream(root / "a.jpg", std::ios::binary) << "a";
  std::ofstream(root / "Sub" / "b.jpg", std::ios::binary) << "bb";

  MockEventQueue events;
  auto *stream = new CapturingStream;
  TestProxy1_9 proxy("client", stream, reinterpret_cast<Server *>(0x1), &events);
  stream->take();

  QVERIFY(proxy.startFileDrag({root.string()}));
  const auto codes = pumpUntil(proxy, stream, "DGFE", 2);
  QCOMPARE(countCode(codes, "DGBN"), 1L);
  QCOMPARE(countCode(codes, "DGDT"), 2L); // a.jpg and Sub/b.jpg
  QCOMPARE(countCode(codes, "DGFE"), 2L);
  QCOMPARE(countCode(codes, "DGCN"), 0L);
}

void ClientProxyTests::fileDragUnreadableFileCancels()
{
  using namespace deskflow::filedrag;
  QTemporaryDir dir;
  const std::string path = dir.filePath("secret.bin").toStdString();
  std::ofstream(path, std::ios::binary) << "data";
  std::filesystem::permissions(path, std::filesystem::perms::none);

  MockEventQueue events;
  auto *stream = new CapturingStream;
  TestProxy1_9 proxy("client", stream, reinterpret_cast<Server *>(0x1), &events);
  stream->take();

  // the manifest only needs metadata, so the drag still starts...
  QVERIFY(proxy.startFileDrag({path}));
  // ...and the read failure turns into a cancel instead of an exception
  const auto codes = pumpUntil(proxy, stream, "DGCN", 1);
  QCOMPARE(countCode(codes, "DGCN"), 1L);
  QCOMPARE(countCode(codes, "DGDT"), 0L);
  std::filesystem::permissions(path, std::filesystem::perms::owner_all);
}

void ClientProxyTests::fileDragClientStateAndHandOver()
{
  MockEventQueue events;
  auto *stream = new CapturingStream;
  TestProxy1_9 proxy("client", stream, reinterpret_cast<Server *>(0x1), &events);
  stream->take();

  QVERIFY(!proxy.isDraggingFiles());

  // client: "files are being dragged here"
  stream->feed(QByteArray(1, '\x01'));
  QVERIFY(proxy.parseMessage(reinterpret_cast<const uint8_t *>("DGST")));
  QVERIFY(proxy.isDraggingFiles());

  // client: "not any more"
  stream->feed(QByteArray(1, '\x00'));
  QVERIFY(proxy.parseMessage(reinterpret_cast<const uint8_t *>("DGST")));
  QVERIFY(!proxy.isDraggingFiles());

  // handing over asks the client for its files and clears the state
  stream->feed(QByteArray(1, '\x01'));
  QVERIFY(proxy.parseMessage(reinterpret_cast<const uint8_t *>("DGST")));
  proxy.requestFileDragHandOver();
  QCOMPARE(stream->takeCodes(), std::vector<std::string>{"DGHO"});
  QVERIFY(!proxy.isDraggingFiles());
}

void ClientProxyTests::fileDragParallelTransfersTakeTurns()
{
  using namespace deskflow::filedrag;
  QTemporaryDir dir;
  const std::string a = dir.filePath("a.bin").toStdString();
  const std::string b = dir.filePath("b.bin").toStdString();
  const uint32_t chunksEach = 20;
  std::ofstream(a, std::ios::binary) << std::string(kChunkSize * chunksEach, 'a');
  std::ofstream(b, std::ios::binary) << std::string(kChunkSize * chunksEach, 'b');

  MockEventQueue events;
  auto *stream = new CapturingStream;
  TestProxy1_9 proxy("client", stream, reinterpret_cast<Server *>(0x1), &events);
  stream->take();

  // collect messages, pumping until the worker threads caught up
  auto pumpMessages = [&](long dataWanted) {
    std::vector<QByteArray> all;
    for (int i = 0; i < 200; ++i) {
      proxy.pump();
      auto m = stream->takeMessages();
      all.insert(all.end(), m.begin(), m.end());
      if (std::count_if(all.begin(), all.end(), [](const QByteArray &x) { return x.startsWith("DGDT"); }) >= dataWanted)
        break;
      QTest::qWait(5);
    }
    QTest::qWait(20);
    proxy.pump();
    auto m = stream->takeMessages();
    all.insert(all.end(), m.begin(), m.end());
    return all;
  };
  auto dataOf = [](const std::vector<QByteArray> &msgs, uint32_t id) {
    return std::count_if(msgs.begin(), msgs.end(), [id](const QByteArray &m) {
      return m.startsWith("DGDT") && readBe32(m, 4) == id;
    });
  };

  QVERIFY(proxy.startFileDrag({a}));
  auto msgs = pumpMessages(kWindowChunks);
  QCOMPARE(dataOf(msgs, 1), long(kWindowChunks)); // first transfer fills the window

  // a second drag does not cut the first one
  QVERIFY(proxy.startFileDrag({b}));
  msgs = pumpMessages(0);
  QCOMPARE(std::count_if(msgs.begin(), msgs.end(), [](const QByteArray &m) { return m.startsWith("DGBN"); }), 1L);
  QCOMPARE(dataOf(msgs, 1) + dataOf(msgs, 2), 0L); // window still full
  QCOMPARE(std::count_if(msgs.begin(), msgs.end(), [](const QByteArray &m) { return m.startsWith("DGCN"); }), 0L);

  // acking the first transfer lets both take turns within the window
  uint32_t sentA = kWindowChunks;
  uint32_t sentB = 0;
  stream->feed(be32(1) + be32(sentA));
  QVERIFY(proxy.parseMessage(reinterpret_cast<const uint8_t *>("DGAK")));
  msgs = pumpMessages(kWindowChunks);
  const long a1 = dataOf(msgs, 1);
  const long b1 = dataOf(msgs, 2);
  QVERIFY2(a1 > 0 && b1 > 0, "both transfers progress");
  QVERIFY(a1 + b1 <= long(std::max(kWindowChunks, 2 * kAckEveryChunks)));
  sentA += a1;
  sentB += b1;

  // keep acking both until everything was sent
  long fileEnds = 0;
  for (int i = 0; i < 100 && (sentA < chunksEach || sentB < chunksEach); ++i) {
    stream->feed(be32(1) + be32(sentA));
    QVERIFY(proxy.parseMessage(reinterpret_cast<const uint8_t *>("DGAK")));
    stream->feed(be32(2) + be32(sentB));
    QVERIFY(proxy.parseMessage(reinterpret_cast<const uint8_t *>("DGAK")));
    msgs = pumpMessages(1);
    sentA += dataOf(msgs, 1);
    sentB += dataOf(msgs, 2);
    fileEnds += std::count_if(msgs.begin(), msgs.end(), [](const QByteArray &m) { return m.startsWith("DGFE"); });
  }
  QCOMPARE(sentA, chunksEach);
  QCOMPARE(sentB, chunksEach);
  QCOMPARE(fileEnds, 2L);
}

void ClientProxyTests::trackpadEventMessage()
{
  MockEventQueue events;
  auto *stream = new CapturingStream;
  ClientProxy1_10 proxy("client", stream, reinterpret_cast<Server *>(0x1), &events);
  stream->take();

  // serialized events are binary: zero bytes and high bytes must survive
  std::string sent("\x00\x00\x00\x02gesture\x00\xff\x10\x6d", 15);
  sent.append(900, '\x7f');
  proxy.trackpadEvent(sent);

  const auto messages = stream->takeMessages();
  QCOMPARE(messages.size(), size_t(1));
  const QByteArray &msg = messages.front();
  QCOMPARE(msg.left(4), QByteArray("DTPE"));
  QCOMPARE(readBe32(msg, 4), static_cast<uint32_t>(sent.size()));

  // what the client reads back (ServerProxy1_10 uses the same format)
  CapturingStream reader;
  reader.feed(msg.mid(4));
  std::string received;
  QVERIFY(ProtocolUtil::readf(&reader, kMsgDTrackpadEvent + 4, &received));
  QCOMPARE(received, sent);
}

QTEST_MAIN(ClientProxyTests)
