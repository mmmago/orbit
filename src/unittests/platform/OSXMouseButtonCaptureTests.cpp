/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-02 by Lucas Magotteaux; see NOTICE.md.
 */

#include "OSXMouseButtonCaptureTests.h"

#include "platform/OSXMouseButtonCapture.h"

using deskflow::osx::MouseButtonCapture;

namespace {
bool pass(MouseButtonCapture &capture, CGEventType type, bool local, bool tagged = false, int button = 0)
{
  const auto event = CGEventCreate(nullptr);
  CGEventSetType(event, type);
  CGEventSetIntegerValueField(event, kCGMouseEventButtonNumber, button);
  CGEventSetIntegerValueField(event, kCGEventSourceUserData, tagged ? deskflow::osx::filedrag::kSyntheticEventTag : 0);
  const bool result = capture.passToSystem(type, event, local);
  CFRelease(event);
  return result;
}
} // namespace

void OSXMouseButtonCaptureTests::releaseAfterLeaving_data()
{
  QTest::addColumn<bool>("tagged");
  QTest::newRow("finder-drag") << false;
  QTest::newRow("client-server-client-drag") << true;
}

void OSXMouseButtonCaptureTests::releaseAfterLeaving()
{
  QFETCH(bool, tagged);
  MouseButtonCapture capture;
  // The server starts a native drag, then hands it to the client. The
  // physical release must be relayed AND reach the server's window system.
  QVERIFY(pass(capture, kCGEventLeftMouseDown, true, tagged));
  QVERIFY(!pass(capture, kCGEventLeftMouseDragged, false));
  QVERIFY(pass(capture, kCGEventLeftMouseUp, false));
  // The exception belongs to this press only; normal capture resumes.
  QVERIFY(!pass(capture, kCGEventMouseMoved, false));
  QVERIFY(!pass(capture, kCGEventLeftMouseUp, false));
  // A second trip must not inherit a held button from the first.
  QVERIFY(pass(capture, kCGEventLeftMouseDown, true, tagged));
  QVERIFY(pass(capture, kCGEventLeftMouseUp, false));
}

void OSXMouseButtonCaptureTests::remoteClickStaysCaptured()
{
  MouseButtonCapture capture;
  QVERIFY(!pass(capture, kCGEventLeftMouseDown, false));
  QVERIFY(!pass(capture, kCGEventLeftMouseUp, false));
  QVERIFY(pass(capture, kCGEventLeftMouseDown, true));
  QVERIFY(pass(capture, kCGEventLeftMouseUp, true));
  QVERIFY(!pass(capture, kCGEventLeftMouseUp, false));
}

void OSXMouseButtonCaptureTests::releaseMatchesItsOwnButton()
{
  MouseButtonCapture capture;
  QVERIFY(pass(capture, kCGEventLeftMouseDown, true, true));
  QVERIFY(!pass(capture, kCGEventRightMouseUp, false, false, 1));
  QVERIFY(pass(capture, kCGEventLeftMouseUp, false));
  QVERIFY(!pass(capture, kCGEventOtherMouseUp, false, false, 32));
}

void OSXMouseButtonCaptureTests::cancelKeyDoesNotForgetPress()
{
  MouseButtonCapture capture;
  QVERIFY(pass(capture, kCGEventLeftMouseDown, true, true));
  QVERIFY(pass(capture, kCGEventKeyDown, false, true));
  QVERIFY(pass(capture, kCGEventKeyUp, false, true));
  QVERIFY(pass(capture, kCGEventLeftMouseUp, false));
  QVERIFY(!pass(capture, kCGEventLeftMouseUp, false));
}

QTEST_MAIN(OSXMouseButtonCaptureTests)
