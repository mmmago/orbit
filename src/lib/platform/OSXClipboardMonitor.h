/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include "deskflow/IClipboard.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

// The input thread only accesses memory snapshots. All pasteboard calls,
// including construction, synchronization and writes, run on one worker.
class OSXClipboardMonitor
{
public:
  using Snapshot = std::array<std::optional<std::string>, static_cast<size_t>(IClipboard::Format::TotalFormats)>;

  class Backend
  {
  public:
    virtual ~Backend() = default;
    virtual std::optional<Snapshot> read() = 0;
    virtual void write(const Snapshot &) = 0;
  };
  using Factory = std::function<std::unique_ptr<Backend>()>;

  explicit OSXClipboardMonitor(Factory factory = {});
  ~OSXClipboardMonitor();

  bool copyTo(IClipboard *destination) const;
  bool copyFrom(const IClipboard *source);
  bool synchronize();

private:
  struct State;
  static void run(const std::shared_ptr<State> &, const Factory &);

  std::shared_ptr<State> m_state;
  std::thread m_worker;
  uint64_t m_observedGeneration = 0;
  bool m_warned = false;
};
