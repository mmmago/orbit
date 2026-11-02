/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit modifications: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#pragma once

#include "deskflow/IClient.h"

namespace deskflow {
class IStream;
}

//! Generic proxy for client or primary
class BaseClientProxy : public IClient
{
public:
  /*!
  \c name is the name of the client.
  */
  explicit BaseClientProxy(const std::string &name);
  ~BaseClientProxy() override = default;

  //! @name manipulators
  //@{

  //! Save cursor position
  /*!
  Save the position of the cursor when jumping from client.
  */
  void setJumpCursorPos(int32_t x, int32_t y);

  //@}
  //! @name accessors
  //@{

  //! Get cursor position
  /*!
  Get the position of the cursor when last jumping from client.
  */
  void getJumpCursorPos(int32_t &x, int32_t &y) const;

  //! Get cursor position
  /*!
  Return if this proxy is for client or primary.
  */
  virtual bool isPrimary() const
  {
    return false;
  }

  //@}

  // IComputer
  void *getEventTarget() const override = 0;
  bool getClipboard(ClipboardID id, IClipboard *) const override = 0;
  void getShape(int32_t &x, int32_t &y, int32_t &width, int32_t &height) const override = 0;
  void getCursorPos(int32_t &x, int32_t &y) const override = 0;

  // IClient overrides
  void enter(int32_t xAbs, int32_t yAbs, uint32_t seqNum, KeyModifierMask mask, bool forScreensaver) override = 0;
  bool leave() override = 0;
  void setClipboard(ClipboardID, const IClipboard *) override = 0;
  void grabClipboard(ClipboardID) override = 0;
  void setClipboardDirty(ClipboardID, bool) override = 0;
  void keyDown(KeyID, KeyModifierMask, KeyButton, const std::string &) override = 0;
  void keyRepeat(KeyID, KeyModifierMask, int32_t count, KeyButton, const std::string &lang) override = 0;
  void keyUp(KeyID, KeyModifierMask, KeyButton) override = 0;
  void mouseDown(ButtonID) override = 0;
  void mouseUp(ButtonID) override = 0;
  void mouseMove(int32_t xAbs, int32_t yAbs) override = 0;
  void mouseRelativeMove(int32_t xRel, int32_t yRel) override = 0;
  void mouseWheel(int32_t xDelta, int32_t yDelta) override = 0;
  void screensaver(bool activate) override = 0;
  void resetOptions() override = 0;
  void setOptions(const OptionsList &options) override = 0;
  virtual void sendDragInfo(uint32_t fileCount, const char *info, size_t size) = 0;
  virtual void fileChunkSending(uint8_t mark, char *data, size_t dataSize) = 0;
  virtual std::string getSecureInputApp() const = 0;

  //! True if this client can receive files dragged across the edge.
  virtual bool supportsFileDrag() const
  {
    return false;
  }

  //! Announce a file drag (sent right before enter()) and start sending
  //! the files. Returns false if nothing could be sent.
  virtual bool startFileDrag(const std::vector<std::string> &paths)
  {
    return false;
  }

  //! True if the client reported that the user is dragging files on it.
  virtual bool isDraggingFiles() const
  {
    return false;
  }

  //! True if the client can resume its own drag after a return (1.12+).
  virtual bool supportsFileDragReturn() const
  {
    return false;
  }

  //! Resume the client's original drag on its next enter.
  virtual void returnFileDrag()
  {
  }

  //! Token offered by a locked client that can be unlocked from here (1.13+),
  //! empty when it is unlocked or cannot be.
  virtual std::string screenUnlockToken() const
  {
    return {};
  }

  //! Ask the client to type its stored password into its lock screen.
  virtual void unlockScreen(const std::string &)
  {
  }

  //! Ask the client to cancel its local drag and send the dragged files.
  virtual void requestFileDragHandOver()
  {
    // not supported
  }

  //! Relay a serialized trackpad gesture event of the primary (protocol 1.10+).
  virtual void trackpadEvent(const std::string &bytes)
  {
    // not supported before 1.10
  }

  //! True if trackpadEvent() reaches the client
  virtual bool supportsTrackpadEvents() const
  {
    return false;
  }
  virtual void secureInputNotification(const std::string &app) const = 0;
  std::string getName() const override;
  virtual deskflow::IStream *getStream() const = 0;

private:
  std::string m_name;
  int32_t m_x = 0;
  int32_t m_y = 0;
};
