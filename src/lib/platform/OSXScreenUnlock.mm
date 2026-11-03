/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-02 by Lucas Magotteaux; see NOTICE.md.
 */

// compiled with ARC

#include "platform/OSXScreenUnlock.h"

#include "base/Log.h"
#include "deskflow/ScreenUnlock.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Foundation/Foundation.h>
#import <LocalAuthentication/LocalAuthentication.h>
#import <LocalAuthenticationEmbeddedUI/LocalAuthenticationEmbeddedUI.h>
#import <Security/Security.h>

#include <atomic>

namespace deskflow::screenunlock::osx {

namespace {

NSMutableDictionary *passwordQuery()
{
  return [@{
    (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
    (__bridge id)kSecAttrService : @(kKeychainService),
    (__bridge id)kSecAttrAccount : NSUserName(),
  } mutableCopy];
}

std::atomic<uint32_t> s_lastRequest{0};

// main queue only: the request on screen, its context and its panel
uint32_t s_request = 0;
LAContext *s_context = nil;
NSPanel *s_panel = nil;

void closePanel()
{
  [s_panel orderOut:nil];
  s_panel = nil;
}

} // namespace

} // namespace deskflow::screenunlock::osx

// a panel that takes the key focus Touch ID needs without activating Orbit
@interface OrbitTouchIdPanel : NSPanel
@end

@implementation OrbitTouchIdPanel
- (BOOL)canBecomeKeyWindow
{
  return YES;
}
@end

namespace deskflow::screenunlock::osx {

namespace {

// A small HUD at the top of the menu bar screen: the Touch ID glyph and what
// it is for (Apple asks for the reason next to the glyph). Touching the
// sensor answers it; nothing to click or dismiss.
const CGFloat kGlyphSize = 36;

NSPanel *newTouchIdPanel(LAContext *context, NSString *reason) API_AVAILABLE(macos(12.0))
{
  NSTextField *label = [NSTextField labelWithString:reason];
  label.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
  label.textColor = NSColor.labelColor;
  label.lineBreakMode = NSLineBreakByTruncatingTail;

  LAAuthenticationView *glyph = [[LAAuthenticationView alloc] initWithContext:context controlSize:NSControlSizeRegular];

  NSStackView *stack = [NSStackView stackViewWithViews:@[ glyph, label ]];
  stack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  stack.alignment = NSLayoutAttributeCenterY;
  stack.spacing = 8;
  // 10 above and below the glyph, which draws a little outside its frame
  stack.edgeInsets = NSEdgeInsetsMake(18, 12, 18, 16);

  NSVisualEffectView *background = [[NSVisualEffectView alloc] init];
  background.material = NSVisualEffectMaterialHUDWindow;
  background.blendingMode = NSVisualEffectBlendingModeBehindWindow;
  background.state = NSVisualEffectStateActive;
  background.wantsLayer = YES;
  background.layer.cornerRadius = 10;
  background.layer.masksToBounds = YES;
  [background addSubview:stack];
  stack.translatesAutoresizingMaskIntoConstraints = NO;
  [NSLayoutConstraint activateConstraints:@[
    [stack.leadingAnchor constraintEqualToAnchor:background.leadingAnchor],
    [stack.trailingAnchor constraintEqualToAnchor:background.trailingAnchor],
    [stack.topAnchor constraintEqualToAnchor:background.topAnchor],
    [stack.bottomAnchor constraintEqualToAnchor:background.bottomAnchor],
    [label.widthAnchor constraintLessThanOrEqualToConstant:320],
    // twice the height of the text next to it
    [glyph.widthAnchor constraintEqualToConstant:kGlyphSize],
    [glyph.heightAnchor constraintEqualToConstant:kGlyphSize],
  ]];

  const NSSize size = background.fittingSize;
  NSScreen *screen = NSScreen.screens.firstObject ?: NSScreen.mainScreen;
  const NSRect visible = screen.visibleFrame;
  const NSRect frame = NSMakeRect(
      NSMidX(visible) - size.width / 2, NSMaxY(visible) - size.height - 12, size.width, size.height
  );

  OrbitTouchIdPanel *panel = [[OrbitTouchIdPanel alloc]
      initWithContentRect:frame
                styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                  backing:NSBackingStoreBuffered
                    defer:NO];
  panel.contentView = background;
  panel.opaque = NO;
  panel.backgroundColor = NSColor.clearColor;
  panel.hasShadow = YES;
  panel.level = NSStatusWindowLevel;
  panel.hidesOnDeactivate = NO;
  panel.releasedWhenClosed = NO;
  panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                             NSWindowCollectionBehaviorFullScreenAuxiliary | NSWindowCollectionBehaviorStationary;
  [panel orderFrontRegardless];
  [panel makeKeyWindow];
  return panel;
}

} // namespace

uint32_t authenticate(
    const std::string &reason, std::function<void(uint32_t request, bool success)> reply, uint32_t request
)
{
  // only to know now whether Touch ID can be asked; the panel's context is
  // made on the main thread with the panel
  LAContext *probe = [[LAContext alloc] init];
  NSError *error = nil;
  if (![probe canEvaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics error:&error]) {
    LOG_INFO("screen unlock: Touch ID unavailable: %s", error.localizedDescription.UTF8String);
    return 0;
  }

  if (request == 0) {
    request = nextRequest();
  }

  NSString *text = [NSString stringWithUTF8String:reason.c_str()] ?: @"unlock a client";
  // AppKit and the context live on the main thread; this queue is serial, so
  // a later cancel or request always comes after this one
  dispatch_async(dispatch_get_main_queue(), ^{
    [s_context invalidate];
    closePanel();

    LAContext *context = [[LAContext alloc] init];
    // Touch ID only: no "Use Password" button
    context.localizedFallbackTitle = @"";
    s_request = request;
    s_context = context;
    if (@available(macOS 12.0, *)) {
      NSString *title = [NSString stringWithFormat:@"Touch ID to %@", text];
      s_panel = newTouchIdPanel(context, title);
    }

    [context evaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics
            localizedReason:text
                      reply:^(BOOL success, NSError *evaluateError) {
                        if (!success) {
                          LOG_DEBUG(
                              "screen unlock: Touch ID: %s",
                              evaluateError.localizedDescription.UTF8String ?: "failed"
                          );
                        }
                        dispatch_async(dispatch_get_main_queue(), ^{
                          if (s_request == request) {
                            s_request = 0;
                            s_context = nil;
                            closePanel();
                          }
                        });
                        reply(request, success);
                      }];
  });
  return request;
}

uint32_t nextRequest()
{
  uint32_t request = ++s_lastRequest;
  if (request == 0) {
    request = ++s_lastRequest;
  }
  return request;
}

void cancelAuthentication()
{
  dispatch_async(dispatch_get_main_queue(), ^{
    s_request = 0;
    [s_context invalidate];
    s_context = nil;
    closePanel();
  });
}

bool isScreenLocked()
{
  CFDictionaryRef session = CGSessionCopyCurrentDictionary();
  if (session == nullptr) {
    return false;
  }
  bool locked = false;
  const void *value = CFDictionaryGetValue(session, CFSTR("CGSSessionScreenIsLocked"));
  if (value != nullptr && CFGetTypeID(value) == CFBooleanGetTypeID()) {
    locked = CFBooleanGetValue(static_cast<CFBooleanRef>(value));
  } else if (value != nullptr && CFGetTypeID(value) == CFNumberGetTypeID()) {
    int number = 0;
    CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberIntType, &number);
    locked = number != 0;
  }
  CFRelease(session);
  return locked;
}

bool hasPassword()
{
  // an attribute query: no access to the secret, so no Keychain prompt
  NSMutableDictionary *query = passwordQuery();
  query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
  return SecItemCopyMatching((__bridge CFDictionaryRef)query, nullptr) == errSecSuccess;
}

bool loadPassword(std::string &password)
{
  NSMutableDictionary *query = passwordQuery();
  query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
  query[(__bridge id)kSecReturnData] = @YES;
  // nobody can answer a Keychain prompt behind the lock screen
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  query[(__bridge id)kSecUseAuthenticationUI] = (__bridge id)kSecUseAuthenticationUIFail;
#pragma clang diagnostic pop

  CFTypeRef result = nullptr;
  const OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
  if (status != errSecSuccess || result == nullptr) {
    NSString *message = CFBridgingRelease(SecCopyErrorMessageString(status, nullptr));
    LOG_WARN("screen unlock: could not read the password from the Keychain: %s (%d)", message.UTF8String ?: "", status);
    return false;
  }
  NSData *data = CFBridgingRelease(result);
  password.assign(static_cast<const char *>(data.bytes), data.length);
  return !password.empty();
}

} // namespace deskflow::screenunlock::osx
