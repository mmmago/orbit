/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#include "base/Log.h"

#include <QTemporaryDir>
#include <QTest>

class FileDragTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void manifestRoundTrip();
  void manifestRejectsUnsafePaths();
  void manifestRejectsChildBeforeParent();
  void manifestRejectsTruncatedOrOldData();
  void manifestItems();
  void manifestKeepsModesAndSymlinks();
  void manifestRejectsUnsafeLinkTargets();
  void manifestAcceptsVersion2();
  void buildManifestWalksFolders();
  void buildManifestRenamesClashingItems();
  void readerToWriterRoundTripWithFolders();
  void readerToWriterRoundTripKeepsAppBundle();
  void writerRejectsTooMuchData();
  void writerRejectsShortFile();
  void writerDiscardRemovesStaging();
  void manifestAssemblerJoinsParts();
  void manifestAssemblerDropsOversized();
  void previewCanBeDroppedBeforeManifest();
  void cancelledPreviewStaysCancelled();
  void previewRejectsChangedItems();
  void previewCompletesEmptyFolder();
  void clientDragReturnsAcrossEdge_data();
  void clientDragReturnsAcrossEdge();
  void windowGrowsWhileDelayStaysLow();
  void windowHoldsWhenNotLimited();
  void windowShrinksWhenDelayRises();
  void windowStaysWithinBounds();

private:
  Log m_log;
  QTemporaryDir m_tmp;
};
