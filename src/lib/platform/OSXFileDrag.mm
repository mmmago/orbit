/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#import "platform/OSXFileDrag.h"

#import "base/Log.h"
#import "common/Constants.h"

#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>

#include <crt_externs.h>
#include <fcntl.h>
#include <libgen.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <map>
#include <mutex>
#include <thread>

namespace df = deskflow::osx::filedrag;

//
// Source side
//

int64_t df::dragPasteboardChangeCount()
{
  @autoreleasepool {
    return [[NSPasteboard pasteboardWithName:NSPasteboardNameDrag] changeCount];
  }
}

std::vector<std::string> df::draggedFilePaths()
{
  std::vector<std::string> paths;
  @autoreleasepool {
    NSPasteboard *pb = [NSPasteboard pasteboardWithName:NSPasteboardNameDrag];
    NSArray<NSURL *> *urls = [pb readObjectsForClasses:@[ [NSURL class] ]
                                               options:@{
                                                 NSPasteboardURLReadingFileURLsOnlyKey : @YES
                                               }];
    for (NSURL *url in urls) {
      if (url.isFileURL && url.path != nil) {
        paths.emplace_back(url.path.fileSystemRepresentation);
      }
    }
  }
  return paths;
}

void df::postCancelKey()
{
  for (bool down : {true, false}) {
    CGEventRef e = CGEventCreateKeyboardEvent(nullptr, kVK_Escape, down);
    CGEventSetIntegerValueField(e, kCGEventSourceUserData, kSyntheticEventTag);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
  }
}

//
// Receiving side: talks to deskflow-drag-helper (see its main.mm for the protocol).
// deskflow-core must not own windows itself: once a process has windows,
// macOS requires keyboard layout APIs on the main thread, and deskflow-core
// calls them from its event thread (crash in TSMGetInputSourceProperty).
//

namespace {

class Helper
{
public:
  static Helper &instance()
  {
    static Helper helper;
    return helper;
  }

  void prewarm()
  {
    std::scoped_lock lock(m_mutex);
    ensureRunning();
  }

  void begin(
      uint32_t transferId, const std::vector<df::Item> &items, double x, double y, df::Callbacks callbacks,
      bool local = false
  )
  {
    NSMutableArray *list = [NSMutableArray array];
    for (const auto &item : items) {
      [list addObject:@{
        @"name" : [NSString stringWithUTF8String:item.name.c_str()],
        @"size" : @(item.size),
        @"path" : [NSString stringWithUTF8String:item.stagingPath.c_str()],
        @"isDir" : @(item.isDirectory),
        @"count" : @(item.entryCount)
      }];
    }
    std::scoped_lock lock(m_mutex);
    // earlier drags keep their callbacks: their files may still be arriving
    // a local drag delivers nothing itself: the drop target takes the files
    m_sessions[transferId] = {std::move(callbacks), local ? 0 : items.size(), false};
    NSDictionary *message =
        @{@"cmd" : @"begin",
          @"id" : @(transferId),
          @"x" : @(x),
          @"y" : @(y),
          @"local" : @(local),
          @"items" : list};
    if (!send(message)) {
      LOG_ERR("file drag: drag helper is not available");
      endSession(transferId, false);
    }
  }

  void pressAt(uint32_t transferId, double x, double y)
  {
    std::scoped_lock lock(m_mutex);
    send(@{@"cmd" : @"pressAt", @"id" : @(transferId), @"x" : @(x), @"y" : @(y)});
  }

  void itemInfo(uint32_t transferId, size_t index, uint64_t size, uint32_t entries)
  {
    std::lock_guard lock(m_mutex);
    send(@{@"cmd" : @"itemInfo", @"id" : @(transferId), @"index" : @(index), @"size" : @(size), @"count" : @(entries)});
  }

  void itemReady(uint32_t transferId, size_t index)
  {
    std::scoped_lock lock(m_mutex);
    send(@{@"cmd" : @"itemReady", @"id" : @(transferId), @"index" : @(index)});
  }

  void itemProgress(uint32_t transferId, size_t index, uint64_t bytes)
  {
    std::scoped_lock lock(m_mutex);
    send(@{@"cmd" : @"progress", @"id" : @(transferId), @"index" : @(index), @"bytes" : @(bytes)});
  }

  void abort(uint32_t transferId)
  {
    std::scoped_lock lock(m_mutex);
    m_sessions.erase(transferId); // the receiver stopped it: no callbacks needed
    send(@{@"cmd" : @"abort", @"id" : @(transferId)});
  }

private:
  // m_mutex held
  bool ensureRunning()
  {
    if (m_pid > 0) {
      return true;
    }

    char exe[PATH_MAX];
    uint32_t size = sizeof(exe);
    if (_NSGetExecutablePath(exe, &size) != 0) {
      return false;
    }
    const std::string helperPath = std::string(dirname(exe)) + "/" + kAppId + "-drag-helper";

    int toHelper[2];
    int fromHelper[2];
    if (pipe(toHelper) != 0) {
      return false;
    }
    if (pipe(fromHelper) != 0) {
      close(toHelper[0]);
      close(toHelper[1]);
      return false;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, toHelper[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fromHelper[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, toHelper[1]);
    posix_spawn_file_actions_addclose(&actions, fromHelper[0]);

    char *argv[] = {const_cast<char *>(helperPath.c_str()), nullptr};
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, helperPath.c_str(), &actions, nullptr, argv, *_NSGetEnviron());
    posix_spawn_file_actions_destroy(&actions);
    close(toHelper[0]);
    close(fromHelper[1]);
    if (rc != 0) {
      LOG_ERR("file drag: cannot start %s: %s", helperPath.c_str(), strerror(rc));
      close(toHelper[1]);
      close(fromHelper[0]);
      return false;
    }

    fcntl(toHelper[1], F_SETNOSIGPIPE, 1);
    fcntl(toHelper[1], F_SETFD, FD_CLOEXEC);
    fcntl(fromHelper[0], F_SETFD, FD_CLOEXEC);
    m_pid = pid;
    m_toHelper = toHelper[1];
    LOG_DEBUG("file drag: started drag helper (pid %d)", pid);

    std::thread([this, fd = fromHelper[0], pid] { readLoop(fd, pid); }).detach();
    return true;
  }

  // m_mutex held
  bool send(NSDictionary *message)
  {
    for (int attempt = 0; attempt < 2; ++attempt) {
      if (!ensureRunning()) {
        return false;
      }
      NSMutableData *line = [[NSJSONSerialization dataWithJSONObject:message options:0 error:nil] mutableCopy];
      [line appendBytes:"\n" length:1];
      if (write(m_toHelper, line.bytes, line.length) == static_cast<ssize_t>(line.length)) {
        return true;
      }
      // the helper died: start a new one and try once more
      LOG_WARN("file drag: drag helper went away, restarting it");
      resetProcess();
    }
    return false;
  }

  // m_mutex held
  void resetProcess()
  {
    if (m_toHelper >= 0) {
      close(m_toHelper);
      m_toHelper = -1;
    }
    m_pid = 0;
  }

  struct Session
  {
    df::Callbacks callbacks;
    size_t undelivered = 0;
    bool ended = false;
  };

  // m_mutex held: the drag ended without a drop (or could not start)
  void endSession(uint32_t transferId, bool accepted)
  {
    const auto it = m_sessions.find(transferId);
    if (it == m_sessions.end()) {
      return;
    }
    if (!it->second.ended && it->second.callbacks.ended) {
      it->second.callbacks.ended(accepted);
    }
    m_sessions.erase(it);
  }

  // m_mutex held: the helper died; every drag and pending drop is lost
  void failAllSessions()
  {
    for (auto &[id, session] : m_sessions) {
      if (!session.ended) {
        if (session.callbacks.ended) {
          session.callbacks.ended(false);
        }
      } else if (session.callbacks.cancelRequested) {
        session.callbacks.cancelRequested(); // dropped: stop its transfer
      }
    }
    m_sessions.clear();
  }

  void readLoop(int fd, pid_t pid)
  {
    FILE *in = fdopen(fd, "r");
    char *line = nullptr;
    size_t capacity = 0;
    while (getline(&line, &capacity, in) > 0) {
      @autoreleasepool {
        NSData *data = [NSData dataWithBytes:line length:strlen(line)];
        NSDictionary *msg = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
        if ([msg isKindOfClass:[NSDictionary class]]) {
          handle(msg);
        }
      }
    }
    free(line);
    fclose(in);
    waitpid(pid, nullptr, 0);

    std::scoped_lock lock(m_mutex);
    if (m_pid == pid) {
      LOG_WARN("file drag: drag helper exited");
      resetProcess();
      failAllSessions();
    }
  }

  void handle(NSDictionary *msg)
  {
    NSString *evt = msg[@"evt"];
    if ([evt isEqualToString:@"log"]) {
      NSString *level = msg[@"level"];
      const char *text = [msg[@"msg"] UTF8String];
      if ([level isEqualToString:@"warn"]) {
        LOG_WARN("%s", text);
      } else if ([level isEqualToString:@"info"]) {
        LOG_INFO("%s", text);
      } else {
        LOG_DEBUG("%s", text);
      }
      return;
    }
    if ([evt isEqualToString:@"hello"]) {
      LOG_DEBUG("file drag: drag helper ready");
      return;
    }

    const uint32_t transferId = [msg[@"id"] unsignedIntValue];
    std::scoped_lock lock(m_mutex);
    const auto it = m_sessions.find(transferId);
    if (it == m_sessions.end()) {
      return;
    }
    Session &session = it->second;
    if ([evt isEqualToString:@"ready"]) {
      if (session.callbacks.ready) {
        session.callbacks.ready();
      }
    } else if ([evt isEqualToString:@"placed"]) {
      if (session.callbacks.placed) {
        session.callbacks.placed();
      }
    } else if ([evt isEqualToString:@"ended"]) {
      const bool accepted = [msg[@"accepted"] boolValue];
      LOG_INFO("file drag: drop %s (transfer %u)", accepted ? "accepted" : "cancelled or refused", transferId);
      if (!accepted) {
        endSession(transferId, false);
        return;
      }
      session.ended = true;
      if (session.callbacks.ended) {
        session.callbacks.ended(true);
      }
      if (session.undelivered == 0) {
        m_sessions.erase(it); // nothing left to hand over
      }
    } else if ([evt isEqualToString:@"delivered"]) {
      if (session.callbacks.delivered) {
        session.callbacks.delivered([msg[@"index"] unsignedLongValue], [msg[@"ok"] boolValue]);
      }
      if (session.undelivered > 0 && --session.undelivered == 0) {
        m_sessions.erase(it); // all items handed over
      }
    } else if ([evt isEqualToString:@"cancel"]) {
      LOG_INFO("file drag: transfer %u cancelled from Finder", transferId);
      if (session.callbacks.cancelRequested) {
        session.callbacks.cancelRequested();
      }
    }
  }

  std::mutex m_mutex;
  pid_t m_pid = 0;
  int m_toHelper = -1;
  std::map<uint32_t, Session> m_sessions;
};

} // namespace

void df::prewarmReceive()
{
  Helper::instance().prewarm();
}

void df::beginReceive(uint32_t transferId, std::vector<Item> items, double x, double y, Callbacks callbacks)
{
  Helper::instance().begin(transferId, items, x, y, std::move(callbacks));
}

void df::beginLocal(uint32_t dragId, std::vector<Item> items, double x, double y, Callbacks callbacks)
{
  Helper::instance().begin(dragId, items, x, y, std::move(callbacks), true);
}

void df::pressAt(uint32_t transferId, double x, double y)
{
  Helper::instance().pressAt(transferId, x, y);
}

void df::itemInfo(uint32_t transferId, size_t index, uint64_t size, uint32_t entries)
{
  Helper::instance().itemInfo(transferId, index, size, entries);
}

void df::itemReady(uint32_t transferId, size_t index)
{
  Helper::instance().itemReady(transferId, index);
}

void df::itemProgress(uint32_t transferId, size_t index, uint64_t bytes)
{
  Helper::instance().itemProgress(transferId, index, bytes);
}

void df::abortReceive(uint32_t transferId)
{
  Helper::instance().abort(transferId);
}
