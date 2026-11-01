/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2011 Nick Bolton
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OSXClipboardTests.h"

#include "deskflow/Clipboard.h"
#include "platform/OSXClipboard.h"
#include "platform/OSXClipboardBMPConverter.h"
#include "platform/OSXClipboardUTF8Converter.h"

#include <QBuffer>
#include <QImage>
#include <QScopeGuard>

namespace {
const CFStringRef testPasteboardName = CFSTR("org.deskflow.OSXClipboardTests");
}

void OSXClipboardTests::open()
{
  OSXClipboard clipboard(testPasteboardName);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());
  clipboard.close();
}

void OSXClipboardTests::singleFormat()
{
  using enum IClipboard::Format;

  OSXClipboard clipboard(testPasteboardName);
  QVERIFY(clipboard.empty());
  clipboard.add(Text, m_testString);
  QVERIFY(clipboard.has(Text));
  QCOMPARE(clipboard.get(Text), m_testString);
}

void OSXClipboardTests::localBitmap()
{
  QImage image(2, 2, QImage::Format_RGB32);
  image.fill(QColor(10, 20, 30));
  QBuffer buffer;
  QVERIFY(buffer.open(QIODevice::WriteOnly));
  QVERIFY(image.save(&buffer, "BMP"));
  const std::string bmp(buffer.data().constData(), buffer.data().size());

  OSXClipboard clipboard(testPasteboardName);
  QVERIFY(clipboard.empty());
  OSXClipboardBMPConverter converter;
  clipboard.add(IClipboard::Format::Bitmap, converter.toIClipboard(bmp));
  QVERIFY(clipboard.has(IClipboard::Format::Bitmap));
  const auto result = converter.fromIClipboard(clipboard.get(IClipboard::Format::Bitmap));
  const auto decoded = QImage::fromData(reinterpret_cast<const uchar *>(result.data()), result.size(), "BMP");
  QCOMPARE(decoded.size(), image.size());
  QCOMPARE(decoded.pixelColor(0, 0), image.pixelColor(0, 0));
}

void OSXClipboardTests::ignoredClipboard_data()
{
  QTest::addColumn<QString>("marker");
  QTest::addColumn<bool>("secondItem");

  QTest::newRow("local-promised-text") << QString() << false;
  for (const auto *marker : {
           "com.apple.is-remote-clipboard",
           "public.file-url",
           "NSFilenamesPboardType",
           "com.apple.pasteboard.promised-file-url",
           "com.apple.pasteboard.promised-file-content-type",
           "NSFilesPromisePboardType",
       }) {
    QTest::newRow(marker) << QString::fromLatin1(marker) << false;
    QTest::newRow(qPrintable(QString::fromLatin1(marker) + "-second-item")) << QString::fromLatin1(marker) << true;
  }
}

void OSXClipboardTests::ignoredClipboard()
{
  QFETCH(QString, marker);
  QFETCH(bool, secondItem);

  OSXClipboard clipboard(testPasteboardName);
  QVERIFY(clipboard.empty());

  // A separate writer advertises lazy formats. Inspecting/reading an ignored
  // clipboard must never ask it to supply those formats, as Handoff could block.
  PasteboardRef writer = nullptr;
  QCOMPARE(PasteboardCreate(testPasteboardName, &writer), noErr);
  int reads = 0;
  const auto cleanup = qScopeGuard([&] {
    PasteboardClear(writer);
    CFRelease(writer);
  });
  QCOMPARE(PasteboardClear(writer), noErr);
  const auto keepPromise = [](PasteboardRef board, PasteboardItemID item, CFStringRef type, void *context) -> OSStatus {
    ++*static_cast<int *>(context);
    const UInt8 text[] = "deskflow promised text";
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, text, sizeof(text) - 1);
    const OSStatus result = PasteboardPutItemFlavor(board, item, type, data, kPasteboardFlavorNoFlags);
    CFRelease(data);
    return result;
  };
  QCOMPARE(PasteboardSetPromiseKeeper(writer, keepPromise, &reads), noErr);

  for (CFStringRef type : {CFSTR("public.utf8-plain-text"), CFSTR("public.html"), CFSTR("com.microsoft.bmp")}) {
    QCOMPARE(
        PasteboardPutItemFlavor(writer, reinterpret_cast<PasteboardItemID>(1), type, nullptr, kPasteboardFlavorNoFlags),
        noErr
    );
  }
  if (!marker.isEmpty()) {
    const QByteArray utf8 = marker.toUtf8();
    CFStringRef type = CFStringCreateWithCString(kCFAllocatorDefault, utf8.constData(), kCFStringEncodingUTF8);
    const OSStatus result = PasteboardPutItemFlavor(
        writer, reinterpret_cast<PasteboardItemID>(secondItem ? 2 : 1), type, nullptr, kPasteboardFlavorNoFlags
    );
    CFRelease(type);
    QCOMPARE(result, noErr);
  }
  clipboard.synchronize();
  QCOMPARE(reads, 0);

  if (marker.isEmpty()) {
    QVERIFY(clipboard.has(IClipboard::Format::Text));
    QCOMPARE(clipboard.get(IClipboard::Format::Text), "deskflow promised text");
    QCOMPARE(reads, 1);
  } else {
    Clipboard copy;
    QVERIFY(Clipboard::copy(&copy, &clipboard));
    QVERIFY(copy.open(0));
    for (auto format : {IClipboard::Format::Text, IClipboard::Format::HTML, IClipboard::Format::Bitmap}) {
      QVERIFY(!clipboard.has(format));
      QVERIFY(clipboard.get(format).empty());
      QVERIFY(!copy.has(format));
    }
    copy.close();
    QCOMPARE(reads, 0);
  }
}

void OSXClipboardTests::localClipboardAfterIgnored()
{
  // Replacing an ignored clipboard must immediately allow local sharing again.
  OSXClipboard clipboard(testPasteboardName);
  PasteboardRef writer = nullptr;
  QCOMPARE(PasteboardCreate(testPasteboardName, &writer), noErr);
  const auto cleanup = qScopeGuard([&] { CFRelease(writer); });
  QCOMPARE(PasteboardClear(writer), noErr);
  CFDataRef marker = CFDataCreate(kCFAllocatorDefault, nullptr, 0);
  const OSStatus result = PasteboardPutItemFlavor(
      writer, reinterpret_cast<PasteboardItemID>(1), CFSTR("com.apple.is-remote-clipboard"), marker,
      kPasteboardFlavorNoFlags
  );
  CFRelease(marker);
  QCOMPARE(result, noErr);
  clipboard.synchronize();
  QVERIFY(!clipboard.has(IClipboard::Format::Text));

  QVERIFY(clipboard.empty());
  clipboard.add(IClipboard::Format::Text, "file:///tmp/a-path-copied-as-text");
  clipboard.add(IClipboard::Format::HTML, "<b>local text</b>");
  QVERIFY(clipboard.has(IClipboard::Format::Text));
  QCOMPARE(clipboard.get(IClipboard::Format::Text), "file:///tmp/a-path-copied-as-text");
  QCOMPARE(clipboard.get(IClipboard::Format::HTML), "<b>local text</b>");
}

void OSXClipboardTests::formatConvert_UTF8()
{
  OSXClipboardUTF8Converter converter;
  QCOMPARE(IClipboard::Format::Text, converter.getFormat());
  QCOMPARE(converter.getOSXFormat(), CFSTR("public.utf8-plain-text"));
  QCOMPARE(converter.fromIClipboard("test data\n"), "test data\r");
  QCOMPARE(converter.toIClipboard("test data\r"), "test data\n");
}

QTEST_MAIN(OSXClipboardTests)
