/*
 * Orbit -- keyboard, mouse and trackpad sharing between Macs
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-04 by Lucas Magotteaux; see NOTICE.md.
 */

#import "OSXQuitHandler.h"

#import <Cocoa/Cocoa.h>

static void (*s_quitHelper)(void *) = NULL;
static void *s_quitContext = NULL;

@interface OrbitHelperQuitEventHandler : NSObject
- (void)handleQuitEvent:(NSAppleEventDescriptor *)event withReplyEvent:(NSAppleEventDescriptor *)reply;
@end

@implementation OrbitHelperQuitEventHandler
- (void)handleQuitEvent:(NSAppleEventDescriptor *)event withReplyEvent:(NSAppleEventDescriptor *)reply
{
  (void)event;
  (void)reply;
  // The GUI and its helpers share Orbit.app's bundle identity, so Launch
  // Services can deliver the app's quit event to either helper. Address the
  // GUI by its executable URL instead of that shared bundle identifier.
  NSURL *executables = [[[NSRunningApplication currentApplication] executableURL] URLByDeletingLastPathComponent];
  NSURL *guiExecutable = [executables URLByAppendingPathComponent:@"Orbit"];
  for (NSRunningApplication *app in [[NSWorkspace sharedWorkspace] runningApplications]) {
    if (app.processIdentifier != [[NSProcessInfo processInfo] processIdentifier] &&
        [app.executableURL isEqual:guiExecutable] && [app terminate]) {
      return;
    }
  }
  // Without a GUI, let the helper stop its own workers as well as its
  // main loop. In particular, the core must stop its event queue first.
  if (s_quitHelper) {
    s_quitHelper(s_quitContext);
  }
}
@end

static void registerHelperQuitEventHandler()
{
  static OrbitHelperQuitEventHandler *handler = nil;
  if (!handler) {
    handler = [[OrbitHelperQuitEventHandler alloc] init];
  }
  [[NSAppleEventManager sharedAppleEventManager] setEventHandler:handler
                                                   andSelector:@selector(handleQuitEvent:withReplyEvent:)
                                                 forEventClass:kCoreEventClass
                                                    andEventID:kAEQuitApplication];
}

void installCocoaQuitHandler(void (*quit)(void *), void *context)
{
  s_quitHelper = quit;
  s_quitContext = context;
  registerHelperQuitEventHandler();
  // AppKit installs its default handlers during launch, before this
  // notification. Register again so that they cannot replace ours.
  [[NSNotificationCenter defaultCenter] addObserverForName:NSApplicationWillFinishLaunchingNotification
                                                   object:NSApp
                                                    queue:nil
                                               usingBlock:^(NSNotification *note) {
                                                 (void)note;
                                                 registerHelperQuitEventHandler();
                                               }];
}
