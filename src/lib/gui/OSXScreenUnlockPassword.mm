/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-02 by Lucas Magotteaux; see NOTICE.md.
 */

// compiled with ARC

#include "gui/OSXScreenUnlockPassword.h"

#include "common/Constants.h"
#include "deskflow/ScreenUnlock.h"

#import <Foundation/Foundation.h>
#import <OpenDirectory/OpenDirectory.h>
#import <Security/Security.h>

namespace deskflow::gui::screenunlock {

namespace {

NSMutableDictionary *passwordQuery()
{
  return [@{
    (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
    (__bridge id)kSecAttrService : @(deskflow::screenunlock::kKeychainService),
    (__bridge id)kSecAttrAccount : NSUserName(),
  } mutableCopy];
}

bool verifyLoginPassword(NSString *password, QString &error)
{
  NSError *odError = nil;
  ODNode *node = [ODNode nodeWithSession:[ODSession defaultSession] type:kODNodeTypeAuthentication error:&odError];
  ODRecord *record = [node recordWithRecordType:kODRecordTypeUsers name:NSUserName() attributes:nil error:&odError];
  if (record == nil) {
    error = QStringLiteral("Could not check the password: %1")
                .arg(QString::fromNSString(odError.localizedDescription ?: @"user not found"));
    return false;
  }
  if (![record verifyPassword:password error:&odError]) {
    error = QStringLiteral("This is not the password of %1 on this Mac.").arg(QString::fromNSString(NSUserName()));
    return false;
  }
  return true;
}

// Orbit sets the password, orbit-core reads it behind the lock screen: both
// are trusted, so neither is asked. The access follows their signature, so
// it survives updates signed by the same developer.
SecAccessRef newAccess()
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  NSMutableArray *trusted = [NSMutableArray array];
  SecTrustedApplicationRef app = nullptr;
  if (SecTrustedApplicationCreateFromPath(nullptr, &app) == errSecSuccess) {
    [trusted addObject:CFBridgingRelease(app)];
  }
  NSString *core = [NSBundle.mainBundle pathForAuxiliaryExecutable:@(kCoreBinName)];
  SecTrustedApplicationRef coreApp = nullptr;
  if (core != nil && SecTrustedApplicationCreateFromPath(core.fileSystemRepresentation, &coreApp) == errSecSuccess) {
    [trusted addObject:CFBridgingRelease(coreApp)];
  }
  SecAccessRef access = nullptr;
  SecAccessCreate(CFSTR("Orbit screen unlock"), (__bridge CFArrayRef)trusted, &access);
  return access;
#pragma clang diagnostic pop
}

} // namespace

bool hasPassword()
{
  NSMutableDictionary *query = passwordQuery();
  query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
  return SecItemCopyMatching((__bridge CFDictionaryRef)query, nullptr) == errSecSuccess;
}

bool storePassword(const QString &password, QString &error)
{
  NSString *text = password.toNSString();
  if (text.length == 0) {
    error = QStringLiteral("Enter the password of this Mac.");
    return false;
  }
  if (!verifyLoginPassword(text, error)) {
    return false;
  }

  removePassword();
  SecAccessRef access = newAccess();
  NSMutableDictionary *item = passwordQuery();
  item[(__bridge id)kSecAttrLabel] = @"Orbit screen unlock";
  item[(__bridge id)kSecAttrDescription] = @"typed into the lock screen after Touch ID on the Orbit server";
  item[(__bridge id)kSecValueData] = [text dataUsingEncoding:NSUTF8StringEncoding];
  if (access != nullptr) {
    item[(__bridge id)kSecAttrAccess] = CFBridgingRelease(access);
  }
  const OSStatus status = SecItemAdd((__bridge CFDictionaryRef)item, nullptr);
  if (status != errSecSuccess) {
    NSString *message = CFBridgingRelease(SecCopyErrorMessageString(status, nullptr));
    error = QStringLiteral("Could not store the password in the Keychain: %1").arg(QString::fromNSString(message));
    return false;
  }
  return true;
}

void removePassword()
{
  SecItemDelete((__bridge CFDictionaryRef)passwordQuery());
}

} // namespace deskflow::gui::screenunlock
