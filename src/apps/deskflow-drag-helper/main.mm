/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// deskflow-drag-helper: receiving half of a cross-computer file drag (macOS).
//
// deskflow-core starts this process and talks to it with one JSON object per
// line on stdin/stdout:
//
//   core -> helper
//     {"cmd":"begin","id":1,"x":10,"y":20,"items":[{"name":"a.txt","size":3,"path":"/staging/a.txt","isDir":false}]}
//     {"cmd":"pressAt","id":1,"x":10,"y":20}  park the panel there, then report "placed"
//     {"cmd":"progress","id":1,"index":0,"bytes":1024}
//     {"cmd":"itemReady","id":1,"index":0}
//     {"cmd":"abort","id":1}
//
//   helper -> core
//     {"evt":"hello"}
//     {"evt":"ready","id":1}                 panel is on screen, following the cursor
//     {"evt":"placed","id":1}                panel is parked at the pressAt point: press now
//     {"evt":"ended","id":1,"accepted":true} drag session ended
//     {"evt":"delivered","id":1,"index":0,"ok":true}
//     {"evt":"cancel","id":1}                user pressed Finder's cancel button
//     {"evt":"log","level":"info","msg":"..."}
//
// On "begin" it shows a tiny invisible, non-activating panel at (x, y). The
// core then injects a left mouse-down there, which starts a native drag with
// one file promise per item (file or folder). A promise is fulfilled (the
// staged item moved to where the user dropped) once "itemReady" arrived for
// it. Until then a placeholder sits at the destination with published file
// progress, which Finder shows as a dimmed icon with a progress bar and a
// cancel button. The helper exits when stdin closes.

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <sys/xattr.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

std::mutex s_outMutex;

void emit(NSDictionary *message)
{
  NSData *json = [NSJSONSerialization dataWithJSONObject:message options:0 error:nil];
  if (json == nil) {
    return;
  }
  std::scoped_lock lock(s_outMutex);
  fwrite(json.bytes, 1, json.length, stdout);
  fputc('\n', stdout);
  fflush(stdout);
}

void emitLog(NSString *level, NSString *text)
{
  emit(@{@"evt" : @"log", @"level" : level, @"msg" : text});
}

struct Item
{
  NSString *name;
  NSString *path;
  bool isDirectory = false;
  uint64_t size = 0;
  uint32_t count = 1; // files and folders in this item, itself included
};

struct Session
{
  uint32_t transferId = 0;
  std::vector<Item> items;
  std::vector<bool> ready;
  std::vector<uint64_t> bytes;        // received so far, per item
  std::vector<NSProgress *> progress; // published while an item is pending after the drop
  std::vector<bool> placeholder;      // a placeholder was put at the destination
  std::vector<bool> settled;          // its promise finished (delivered or failed)
  std::vector<bool> delivered;        // its promise was answered (with or without placeholder)
  NSString *destination = nil;        // display name of the drop folder
  bool failed = false;
  bool dragStarted = false;
  std::mutex mutex;
  std::condition_variable changed;
};

// Promise writes wait at most this long for data still in transit.
constexpr auto kPromiseTimeout = std::chrono::minutes(30);

// Finder greys out files whose type/creator is 'brok'/'MACS' (its own marker
// for copies in progress). Folders ignore it, so a pending folder is shown as
// a marked empty file with the folder icon until the real folder replaces it.
void markBusy(NSURL *url)
{
  uint8_t info[32] = {'b', 'r', 'o', 'k', 'M', 'A', 'C', 'S'};
  setxattr(url.fileSystemRepresentation, "com.apple.FinderInfo", info, sizeof(info), 0, 0);
}

// The type a dragged item is announced and shown as. Folders whose extension
// names a package type (.app, .bundle, .pkg, ...) keep that type, so they
// drag and land as the package, not as a plain folder.
UTType *typeOfItem(NSString *name, bool isDirectory)
{
  UTType *type = [UTType typeWithFilenameExtension:name.pathExtension];
  if (isDirectory) {
    return (type != nil && [type conformsToType:UTTypePackage]) ? type : UTTypeFolder;
  }
  return type ?: UTTypeData;
}

bool createPlaceholder(NSURL *url, bool isDirectory)
{
  if (![[NSFileManager defaultManager] createFileAtPath:url.path contents:nil attributes:nil]) {
    return false;
  }
  markBusy(url);
  if (isDirectory) {
    dispatch_sync(dispatch_get_main_queue(), ^{
      [[NSWorkspace sharedWorkspace]
          setIcon:[[NSWorkspace sharedWorkspace] iconForContentType:typeOfItem(url.lastPathComponent, true)]
          forFile:url.path
          options:0];
    });
  }
  return true;
}

//
// Copy window: a Finder-style progress window for items still arriving
// after the drop. Main thread only.
//

void removeSession(uint32_t transferId);

// The helper is not a localized bundle, so AppKit and Foundation would show
// their own words in English. Its few strings come from the table below, in
// the first of the user's preferred languages it has; sizes and durations go
// through ICU-backed formatters given that language's locale.
NSDictionary<NSString *, NSDictionary<NSString *, NSString *> *> *stringTable()
{
  // copyOne: name, destination; copyMany: count, destination; progress: done,
  // total; about: a duration
  static NSDictionary *table = @{
    @"en" : @{@"copyOne" : @"Copying “%1$@” to “%2$@”", @"copyMany" : @"Copying %1$@ items to “%2$@”",
              @"progress" : @"%1$@ of %2$@", @"estimating" : @"Estimating time remaining…",
              @"about" : @"About %@", @"stop" : @"Stop"},
    @"fr" : @{@"copyOne" : @"Copie de « %1$@ » dans « %2$@ »", @"copyMany" : @"Copie de %1$@ éléments dans « %2$@ »",
              @"progress" : @"%1$@ sur %2$@", @"estimating" : @"Estimation du temps restant…",
              @"about" : @"Environ %@", @"stop" : @"Arrêter"},
    @"de" : @{@"copyOne" : @"„%1$@“ wird nach „%2$@“ kopiert", @"copyMany" : @"%1$@ Objekte werden nach „%2$@“ kopiert",
              @"progress" : @"%1$@ von %2$@", @"estimating" : @"Verbleibende Zeit wird berechnet …",
              @"about" : @"Etwa %@", @"stop" : @"Stoppen"},
    @"es" : @{@"copyOne" : @"Copiando «%1$@» en «%2$@»", @"copyMany" : @"Copiando %1$@ ítems en «%2$@»",
              @"progress" : @"%1$@ de %2$@", @"estimating" : @"Calculando el tiempo restante…",
              @"about" : @"Aproximadamente %@", @"stop" : @"Detener"},
    @"it" : @{@"copyOne" : @"Copia di “%1$@” in “%2$@”", @"copyMany" : @"Copia di %1$@ elementi in “%2$@”",
              @"progress" : @"%1$@ di %2$@", @"estimating" : @"Stima del tempo rimanente…",
              @"about" : @"Circa %@", @"stop" : @"Interrompi"},
    @"pt" : @{@"copyOne" : @"Copiando “%1$@” para “%2$@”", @"copyMany" : @"Copiando %1$@ itens para “%2$@”",
              @"progress" : @"%1$@ de %2$@", @"estimating" : @"Estimando o tempo restante…",
              @"about" : @"Cerca de %@", @"stop" : @"Parar"},
    @"nl" : @{@"copyOne" : @"‘%1$@’ wordt gekopieerd naar ‘%2$@’", @"copyMany" : @"%1$@ onderdelen worden gekopieerd naar ‘%2$@’",
              @"progress" : @"%1$@ van %2$@", @"estimating" : @"Resterende tijd wordt geschat…",
              @"about" : @"Ongeveer %@", @"stop" : @"Stop"},
    @"sv" : @{@"copyOne" : @"Kopierar ”%1$@” till ”%2$@”", @"copyMany" : @"Kopierar %1$@ objekt till ”%2$@”",
              @"progress" : @"%1$@ av %2$@", @"estimating" : @"Beräknar återstående tid…",
              @"about" : @"Ungefär %@", @"stop" : @"Stoppa"},
    @"da" : @{@"copyOne" : @"Kopierer “%1$@” til “%2$@”", @"copyMany" : @"Kopierer %1$@ emner til “%2$@”",
              @"progress" : @"%1$@ af %2$@", @"estimating" : @"Anslår resterende tid…",
              @"about" : @"Ca. %@", @"stop" : @"Stop"},
    @"nb" : @{@"copyOne" : @"Kopierer «%1$@» til «%2$@»", @"copyMany" : @"Kopierer %1$@ objekter til «%2$@»",
              @"progress" : @"%1$@ av %2$@", @"estimating" : @"Beregner gjenstående tid …",
              @"about" : @"Omtrent %@", @"stop" : @"Stopp"},
    @"fi" : @{@"copyOne" : @"Kopioidaan ”%1$@” kohteeseen ”%2$@”", @"copyMany" : @"Kopioidaan %1$@ kohdetta kohteeseen ”%2$@”",
              @"progress" : @"%1$@ / %2$@", @"estimating" : @"Arvioidaan jäljellä olevaa aikaa…",
              @"about" : @"Noin %@", @"stop" : @"Pysäytä"},
    @"pl" : @{@"copyOne" : @"Kopiowanie „%1$@” do „%2$@”", @"copyMany" : @"Kopiowanie rzeczy (%1$@) do „%2$@”",
              @"progress" : @"%1$@ z %2$@", @"estimating" : @"Szacowanie pozostałego czasu…",
              @"about" : @"Pozostało około %@", @"stop" : @"Zatrzymaj"},
    @"ru" : @{@"copyOne" : @"Копирование «%1$@» в «%2$@»", @"copyMany" : @"Копирование объектов (%1$@) в «%2$@»",
              @"progress" : @"%1$@ из %2$@", @"estimating" : @"Оценка оставшегося времени…",
              @"about" : @"Осталось примерно %@", @"stop" : @"Остановить"},
    @"uk" : @{@"copyOne" : @"Копіювання «%1$@» до «%2$@»", @"copyMany" : @"Копіювання об’єктів (%1$@) до «%2$@»",
              @"progress" : @"%1$@ з %2$@", @"estimating" : @"Оцінка часу, що залишився…",
              @"about" : @"Залишилось приблизно %@", @"stop" : @"Зупинити"},
    @"cs" : @{@"copyOne" : @"Kopírování „%1$@“ do „%2$@“", @"copyMany" : @"Kopírování položek (%1$@) do „%2$@“",
              @"progress" : @"%1$@ z %2$@", @"estimating" : @"Odhadování zbývajícího času…",
              @"about" : @"Zbývá přibližně %@", @"stop" : @"Zastavit"},
    @"tr" : @{@"copyOne" : @"“%1$@”, “%2$@” konumuna kopyalanıyor", @"copyMany" : @"%1$@ öğe “%2$@” konumuna kopyalanıyor",
              @"progress" : @"%1$@ / %2$@", @"estimating" : @"Kalan süre hesaplanıyor…",
              @"about" : @"Yaklaşık %@", @"stop" : @"Durdur"},
    @"ja" : @{@"copyOne" : @"“%1$@”を“%2$@”にコピー中", @"copyMany" : @"%1$@項目を“%2$@”にコピー中",
              @"progress" : @"%1$@/%2$@", @"estimating" : @"残り時間を計算中…",
              @"about" : @"約%@", @"stop" : @"停止"},
    @"ko" : @{@"copyOne" : @"‘%1$@’을(를) ‘%2$@’(으)로 복사 중", @"copyMany" : @"%1$@개 항목을 ‘%2$@’(으)로 복사 중",
              @"progress" : @"%1$@/%2$@", @"estimating" : @"남은 시간 계산 중…",
              @"about" : @"약 %@", @"stop" : @"중단"},
    @"zh-Hans" : @{@"copyOne" : @"正在将“%1$@”拷贝到“%2$@”", @"copyMany" : @"正在将 %1$@ 个项目拷贝到“%2$@”",
                   @"progress" : @"%1$@/%2$@", @"estimating" : @"正在估算剩余时间…",
                   @"about" : @"大约%@", @"stop" : @"停止"},
    @"zh-Hant" : @{@"copyOne" : @"正在將「%1$@」拷貝到「%2$@」", @"copyMany" : @"正在將 %1$@ 個項目拷貝到「%2$@」",
                   @"progress" : @"%1$@/%2$@", @"estimating" : @"正在估計剩餘時間⋯",
                   @"about" : @"大約%@", @"stop" : @"停止"},
  };
  return table;
}

//! The table's language closest to the user's preferred languages
NSString *uiLanguage()
{
  NSString *language =
      [NSBundle preferredLocalizationsFromArray:stringTable().allKeys forPreferences:NSLocale.preferredLanguages]
          .firstObject;
  return language ?: @"en";
}

NSString *localized(NSString *key)
{
  NSString *text = stringTable()[uiLanguage()][key];
  return text ?: stringTable()[@"en"][key];
}

//! The user's first preferred language with this Mac's region: its words,
//! plurals and number format (currentLocale would carry English, the
//! helper's own language)
NSLocale *uiLocale()
{
  static NSLocale *locale = nil;
  if (locale != nil) {
    return locale;
  }
  NSString *preferred = NSLocale.preferredLanguages.firstObject ?: @"en";
  NSMutableDictionary *parts = [[NSLocale componentsFromLocaleIdentifier:preferred] mutableCopy];
  if (NSString *region = NSLocale.currentLocale.countryCode; region != nil && parts[NSLocaleCountryCode] == nil) {
    parts[NSLocaleCountryCode] = region;
  }
  locale = [NSLocale localeWithLocaleIdentifier:[NSLocale localeIdentifierFromComponents:parts]];
  return locale;
}

// decimal units like Finder: no decimals up to KB, one for MB, two above
NSString *formatBytes(uint64_t bytes)
{
  NSUnitInformationStorage *const units[] = {
      NSUnitInformationStorage.bytes, NSUnitInformationStorage.kilobytes, NSUnitInformationStorage.megabytes,
      NSUnitInformationStorage.gigabytes, NSUnitInformationStorage.terabytes
  };
  double value = static_cast<double>(bytes);
  int unit = 0;
  while (value >= 1000 && unit < 4) {
    value /= 1000;
    ++unit;
  }
  NSLocale *locale = uiLocale();
  NSMeasurementFormatter *mf = [[NSMeasurementFormatter alloc] init];
  mf.locale = locale;
  mf.unitOptions = NSMeasurementFormatterUnitOptionsProvidedUnit;
  // "512 bytes", "12.3 MB"
  mf.unitStyle = unit == 0 ? NSFormattingUnitStyleLong : NSFormattingUnitStyleMedium;
  const NSUInteger decimals = unit < 2 ? 0 : (unit == 2 ? 1 : 2);
  mf.numberFormatter.locale = locale;
  mf.numberFormatter.numberStyle = NSNumberFormatterDecimalStyle;
  mf.numberFormatter.minimumFractionDigits = decimals;
  mf.numberFormatter.maximumFractionDigits = decimals;
  return [mf stringFromMeasurement:[[NSMeasurement alloc] initWithDoubleValue:value unit:units[unit]]];
}

NSString *formatCount(uint32_t count)
{
  NSNumberFormatter *nf = [[NSNumberFormatter alloc] init];
  nf.numberStyle = NSNumberFormatterDecimalStyle;
  nf.locale = uiLocale();
  return [nf stringFromNumber:@(count)];
}

NSString *formatRemaining(double seconds)
{
  NSCalendar *calendar = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];
  calendar.locale = uiLocale();
  NSDateComponentsFormatter *df = [[NSDateComponentsFormatter alloc] init];
  df.calendar = calendar;
  df.unitsStyle = NSDateComponentsFormatterUnitsStyleFull;
  df.maximumUnitCount = 1;
  double value = 0;
  if (seconds < 60) {
    df.allowedUnits = NSCalendarUnitSecond;
    value = std::max(1.0, std::ceil(seconds));
  } else if (seconds < 3600) {
    df.allowedUnits = NSCalendarUnitMinute;
    value = std::round(seconds / 60) * 60;
  } else {
    df.allowedUnits = NSCalendarUnitHour;
    value = std::round(seconds / 3600) * 3600;
  }
  return [NSString stringWithFormat:localized(@"about"), [df stringFromTimeInterval:value]];
}

} // namespace

//! Finder-style progress bar. NSProgressIndicator draws grey in a window that is
//! not active, and this panel never activates, so the bar is drawn here.
@interface DFProgressBar : NSView
@property(nonatomic) double doubleValue; // 0...1
@end

@implementation DFProgressBar
- (void)setDoubleValue:(double)value
{
  _doubleValue = std::clamp(value, 0.0, 1.0);
  self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)dirty
{
  constexpr CGFloat kThickness = 8;
  const NSRect track =
      NSMakeRect(0, std::round((NSHeight(self.bounds) - kThickness) / 2), NSWidth(self.bounds), kThickness);
  const CGFloat radius = kThickness / 2;
  [[NSColor.labelColor colorWithAlphaComponent:0.12] setFill];
  [[NSBezierPath bezierPathWithRoundedRect:track xRadius:radius yRadius:radius] fill];
  if (_doubleValue > 0) {
    NSRect fill = track;
    fill.size.width = std::max(NSWidth(track) * _doubleValue, kThickness);
    [NSColor.controlAccentColor setFill];
    [[NSBezierPath bezierPathWithRoundedRect:fill xRadius:radius yRadius:radius] fill];
  }
}
@end

@interface DFCopyActions : NSObject
- (void)cancel:(NSButton *)sender;
@end

@implementation DFCopyActions
- (void)cancel:(NSButton *)sender
{
  emit(@{@"evt" : @"cancel", @"id" : @(static_cast<uint32_t>(sender.tag))});
}
@end

namespace {

// One row per transfer still arriving after its drop, stacked like Finder's
// copy window.
// Metrics measured from Finder's own copy window (points): 404 x 88 for one
// row, 32 pt icon, 11 pt text, 8 pt bar.
constexpr CGFloat kCopyWidth = 404;
constexpr CGFloat kCopyRowHeight = 60;
constexpr CGFloat kCopyTitlebar = 28;
constexpr double kCopyShowDelay = 0.5; // like Finder: no window for quick copies

struct CopyRow
{
  uint32_t transferId = 0;
  NSView *view = nil;
  NSImageView *icon = nil;
  NSTextField *title = nil;
  NSTextField *detail = nil;
  DFProgressBar *bar = nil;
  double rateBytes = 0;
  double rateTime = 0;
  double rate = 0; // bytes per second, smoothed
};

struct Tracked
{
  std::weak_ptr<Session> session;
  CFAbsoluteTime since = 0;
};

NSPanel *s_copyPanel = nil;
DFCopyActions *s_copyActions = nil;
std::vector<CopyRow> s_copyRows;
std::vector<Tracked> s_copyTracked;

NSTextField *makeLabel(NSRect frame, NSFont *font, NSColor *color)
{
  NSTextField *label = [NSTextField labelWithString:@""];
  label.frame = frame;
  label.font = font;
  label.textColor = color;
  label.lineBreakMode = NSLineBreakByTruncatingMiddle;
  return label;
}

void ensureCopyWindow()
{
  if (s_copyPanel != nil) {
    return;
  }
  s_copyPanel = [[NSPanel alloc]
      initWithContentRect:NSMakeRect(0, 0, kCopyWidth, kCopyTitlebar + kCopyRowHeight)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
                          NSWindowStyleMaskNonactivatingPanel | NSWindowStyleMaskFullSizeContentView
                  backing:NSBackingStoreBuffered
                    defer:NO];
  s_copyPanel.titlebarAppearsTransparent = YES;
  s_copyPanel.titleVisibility = NSWindowTitleHidden;
  s_copyPanel.movableByWindowBackground = YES;
  s_copyPanel.hidesOnDeactivate = NO;
  s_copyPanel.releasedWhenClosed = NO;
  s_copyPanel.becomesKeyOnlyIfNeeded = YES;
  [s_copyPanel standardWindowButton:NSWindowZoomButton].enabled = NO;
  s_copyActions = [[DFCopyActions alloc] init];
}

CopyRow makeRow(uint32_t transferId)
{
  CopyRow row;
  row.transferId = transferId;
  row.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, kCopyWidth, kCopyRowHeight)];

  row.icon = [NSImageView imageViewWithImage:[[NSWorkspace sharedWorkspace] iconForContentType:UTTypeFolder]];
  row.icon.frame = NSMakeRect(18, 16, 32, 32);
  row.icon.imageScaling = NSImageScaleProportionallyUpOrDown;
  [row.view addSubview:row.icon];

  row.title = makeLabel(
      NSMakeRect(62, 40, 330, 14), [NSFont systemFontOfSize:11], NSColor.labelColor
  );
  [row.view addSubview:row.title];

  row.bar = [[DFProgressBar alloc] initWithFrame:NSMakeRect(62, 27, 309, 10)];
  [row.view addSubview:row.bar];

  NSButton *cancel = [NSButton buttonWithImage:[NSImage imageNamed:NSImageNameStopProgressFreestandingTemplate]
                                        target:s_copyActions
                                        action:@selector(cancel:)];
  cancel.bordered = NO;
  cancel.imageScaling = NSImageScaleProportionallyUpOrDown;
  cancel.frame = NSMakeRect(379, 25, 14, 14);
  cancel.tag = transferId;
  cancel.toolTip = localized(@"stop");
  [row.view addSubview:cancel];

  row.detail = makeLabel(NSMakeRect(62, 9, 330, 14), [NSFont systemFontOfSize:11], NSColor.secondaryLabelColor);
  [row.view addSubview:row.detail];
  return row;
}

struct CopyTotals
{
  uint64_t total = 0;
  uint64_t done = 0;
  uint32_t count = 0;
  size_t items = 0;
  bool pending = false;
  NSString *singleName = nil;
  bool singleIsDirectory = false;
  NSString *destination = @"";
};

CopyTotals totalsOf(Session &session)
{
  CopyTotals t;
  std::scoped_lock lock(session.mutex);
  for (size_t i = 0; i < session.items.size(); ++i) {
    if (!session.placeholder[i]) {
      continue;
    }
    ++t.items;
    t.total += session.items[i].size;
    t.done += session.settled[i] ? session.items[i].size : std::min(session.bytes[i], session.items[i].size);
    t.count += session.items[i].count;
    t.pending = t.pending || !session.settled[i];
    t.singleName = session.items[i].name;
    t.singleIsDirectory = session.items[i].isDirectory;
  }
  t.destination = session.destination ?: @"";
  return t;
}

void updateRow(CopyRow &row, const CopyTotals &t)
{
  if (t.count == 1 && t.items == 1) {
    row.title.stringValue = [NSString stringWithFormat:localized(@"copyOne"), t.singleName, t.destination];
  } else {
    row.title.stringValue = [NSString stringWithFormat:localized(@"copyMany"), formatCount(t.count), t.destination];
  }
  row.icon.image = [[NSWorkspace sharedWorkspace]
      iconForContentType:t.items == 1 ? typeOfItem(t.singleName, t.singleIsDirectory) : UTTypeFolder];
  row.bar.doubleValue = t.total > 0 ? static_cast<double>(t.done) / static_cast<double>(t.total) : 0;

  const double now = CFAbsoluteTimeGetCurrent();
  if (row.rateTime == 0) {
    row.rateTime = now;
    row.rateBytes = static_cast<double>(t.done);
  } else if (now - row.rateTime >= 0.25) {
    const double instant = (static_cast<double>(t.done) - row.rateBytes) / (now - row.rateTime);
    row.rate = row.rate == 0 ? instant : 0.7 * row.rate + 0.3 * instant;
    row.rateTime = now;
    row.rateBytes = static_cast<double>(t.done);
  }

  NSString *sizes = [NSString stringWithFormat:localized(@"progress"), formatBytes(t.done), formatBytes(t.total)];
  NSString *remaining = localized(@"estimating");
  if (row.rate > 0) {
    remaining = formatRemaining(static_cast<double>(t.total - t.done) / row.rate);
  }
  row.detail.stringValue = [NSString stringWithFormat:@"%@ – %@", sizes, remaining];
}

void refreshCopyWindow()
{
  // transfers still arriving after their drop, oldest first
  const CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
  std::vector<std::pair<uint32_t, CopyTotals>> active;
  std::erase_if(s_copyTracked, [](const Tracked &t) { return t.session.expired(); });
  for (auto &tracked : s_copyTracked) {
    auto session = tracked.session.lock();
    const CopyTotals totals = totalsOf(*session);
    if (totals.pending && now - tracked.since >= kCopyShowDelay) {
      active.emplace_back(session->transferId, totals);
    }
  }
  std::erase_if(s_copyTracked, [](const Tracked &t) {
    auto session = t.session.lock();
    return !session || !totalsOf(*session).pending;
  });

  if (active.empty()) {
    for (auto &row : s_copyRows) {
      [row.view removeFromSuperview];
    }
    s_copyRows.clear();
    [s_copyPanel orderOut:nil];
    return;
  }

  ensureCopyWindow();
  std::vector<CopyRow> rows;
  for (const auto &[id, totals] : active) {
    auto it =
        std::find_if(s_copyRows.begin(), s_copyRows.end(), [id = id](const CopyRow &r) { return r.transferId == id; });
    rows.push_back(it != s_copyRows.end() ? *it : makeRow(id));
  }
  for (auto &old : s_copyRows) {
    if (std::none_of(rows.begin(), rows.end(), [&](const CopyRow &r) { return r.transferId == old.transferId; })) {
      [old.view removeFromSuperview];
    }
  }
  s_copyRows = std::move(rows);

  // resize keeping the top edge, rows stacked from the top
  const CGFloat height = kCopyTitlebar + kCopyRowHeight * static_cast<CGFloat>(s_copyRows.size());
  const bool wasVisible = s_copyPanel.visible;
  NSRect frame = [s_copyPanel frameRectForContentRect:NSMakeRect(0, 0, kCopyWidth, height)];
  if (wasVisible) {
    frame.origin = NSMakePoint(s_copyPanel.frame.origin.x, NSMaxY(s_copyPanel.frame) - frame.size.height);
  } else {
    const NSRect screen = NSScreen.mainScreen.visibleFrame;
    frame.origin = NSMakePoint(NSMidX(screen) - frame.size.width / 2, NSMaxY(screen) - frame.size.height - 120);
  }
  [s_copyPanel setFrame:frame display:YES];
  NSView *content = s_copyPanel.contentView;
  for (size_t i = 0; i < s_copyRows.size(); ++i) {
    auto &row = s_copyRows[i];
    const CGFloat y = height - kCopyTitlebar - kCopyRowHeight * static_cast<CGFloat>(i + 1);
    row.view.frame = NSMakeRect(0, y, kCopyWidth, kCopyRowHeight);
    if (row.view.superview != content) {
      [content addSubview:row.view];
    }
    updateRow(row, active[i].second);
  }
  if (!wasVisible) {
    [s_copyPanel orderFrontRegardless];
  }
}

// A promise of this session waits for data: give it a row if it lasts.
void trackCopy(const std::shared_ptr<Session> &session)
{
  const bool known = std::any_of(s_copyTracked.begin(), s_copyTracked.end(), [&](const Tracked &t) {
    return t.session.lock() == session;
  });
  if (!known) {
    s_copyTracked.push_back({session, CFAbsoluteTimeGetCurrent()});
  }
  dispatch_after(
      dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(kCopyShowDelay * NSEC_PER_SEC) + 20 * NSEC_PER_MSEC),
      dispatch_get_main_queue(),
      ^{
        refreshCopyWindow();
      }
  );
}

void refreshCopyWindowSoon()
{
  dispatch_async(dispatch_get_main_queue(), ^{
    refreshCopyWindow();
  });
}

} // namespace

//! Identifies a promised item: its session (there can be several, the
//! files of earlier drops may still be arriving) and its index.
@interface DFItemRef : NSObject
@property(nonatomic) std::shared_ptr<Session> session;
@property(nonatomic) size_t index;
@end

@implementation DFItemRef
@end

@interface DFDragView : NSView <NSDraggingSource, NSFilePromiseProviderDelegate>
@property(nonatomic) std::shared_ptr<Session> session;
@property(nonatomic, strong) NSOperationQueue *writeQueue;
@end

@implementation DFDragView

- (BOOL)acceptsFirstMouse:(NSEvent *)event
{
  return YES;
}

- (void)drawRect:(NSRect)dirtyRect
{
  // Fully transparent pixels would let the click fall through to the app below.
  [[NSColor colorWithWhite:0.5 alpha:0.02] setFill];
  NSRectFill(dirtyRect);
}

- (void)mouseDown:(NSEvent *)event
{
  auto session = self.session;
  if (!session || session->dragStarted) {
    [self.window orderOut:nil];
    return;
  }

  NSPoint loc = [self convertPoint:event.locationInWindow fromView:nil];
  NSMutableArray<NSDraggingItem *> *dragItems = [NSMutableArray array];
  for (size_t i = 0; i < session->items.size(); ++i) {
    NSString *name = session->items[i].name;
    UTType *type = typeOfItem(name, session->items[i].isDirectory);
    NSFilePromiseProvider *provider = [[NSFilePromiseProvider alloc] initWithFileType:type.identifier delegate:self];
    DFItemRef *ref = [[DFItemRef alloc] init];
    ref.session = session;
    ref.index = i;
    provider.userInfo = ref;

    NSImage *icon = [[NSWorkspace sharedWorkspace] iconForContentType:type];
    icon.size = NSMakeSize(64, 64);
    NSDraggingItem *dragItem = [[NSDraggingItem alloc] initWithPasteboardWriter:provider];
    CGFloat offset = static_cast<CGFloat>(i) * 6;
    [dragItem setDraggingFrame:NSMakeRect(loc.x - 32 + offset, loc.y - 32 - offset, 64, 64) contents:icon];
    [dragItems addObject:dragItem];
  }

  NSDraggingSession *dragSession = [self beginDraggingSessionWithItems:dragItems event:event source:self];
  dragSession.animatesToStartingPositionsOnCancelOrFail = NO;
  dragSession.draggingFormation = NSDraggingFormationPile;
  session->dragStarted = true;
  [self.window orderOut:nil];
  emitLog(
      @"debug", [NSString stringWithFormat:@"file drag: drag session started with %zu item(s)", session->items.size()]
  );
}

- (NSDragOperation)draggingSession:(NSDraggingSession *)s sourceOperationMaskForDraggingContext:(NSDraggingContext)c
{
  return c == NSDraggingContextOutsideApplication ? NSDragOperationCopy : NSDragOperationNone;
}

- (void)draggingSession:(NSDraggingSession *)s endedAtPoint:(NSPoint)p operation:(NSDragOperation)operation
{
  auto session = self.session;
  if (!session) {
    return;
  }
  emit(@{@"evt" : @"ended", @"id" : @(session->transferId), @"accepted" : @(operation != NSDragOperationNone)});
  if (operation == NSDragOperationNone) {
    removeSession(session->transferId);
  }
}

- (NSString *)filePromiseProvider:(NSFilePromiseProvider *)provider fileNameForType:(NSString *)fileType
{
  DFItemRef *ref = provider.userInfo;
  auto session = ref.session;
  const size_t i = ref.index;
  return (session && i < session->items.size()) ? session->items[i].name : @"file";
}

- (NSOperationQueue *)operationQueueForFilePromiseProvider:(NSFilePromiseProvider *)provider
{
  return self.writeQueue;
}

- (void)filePromiseProvider:(NSFilePromiseProvider *)provider
          writePromiseToURL:(NSURL *)url
          completionHandler:(void (^)(NSError *))completionHandler
{
  DFItemRef *ref = provider.userInfo;
  auto session = ref.session;
  const size_t i = ref.index;
  if (!session || i >= session->items.size()) {
    completionHandler([NSError errorWithDomain:NSCocoaErrorDomain code:NSFileWriteUnknownError userInfo:nil]);
    return;
  }

  const Item &item = session->items[i];
  NSFileManager *fm = [NSFileManager defaultManager];

  // Not there yet: put a greyed-out placeholder at the destination, publish
  // its progress (bar and cancel button in Finder's icon views) and track it
  // in the copy window.
  NSProgress *progress = nil;
  bool needPlaceholder = false;
  {
    std::scoped_lock lock(session->mutex);
    needPlaceholder = !session->ready[i] && !session->failed;
  }
  if (needPlaceholder && createPlaceholder(url, item.isDirectory)) {
    std::unique_lock lock(session->mutex);
    session->placeholder[i] = true;
    if (session->destination == nil) {
      session->destination = [fm displayNameAtPath:url.URLByDeletingLastPathComponent.path];
    }
    {
      progress = [NSProgress progressWithTotalUnitCount:static_cast<int64_t>(std::max<uint64_t>(item.size, 1))];
      progress.kind = NSProgressKindFile;
      progress.fileOperationKind = NSProgressFileOperationKindReceiving;
      [progress setUserInfoObject:url forKey:NSProgressFileURLKey];
      progress.completedUnitCount = static_cast<int64_t>(session->bytes[i]);
      progress.cancellable = YES;
      const uint32_t transferId = session->transferId;
      progress.cancellationHandler = ^{
        emit(@{@"evt" : @"cancel", @"id" : @(transferId)});
      };
      session->progress[i] = progress;
    }
  }
  if (progress != nil) {
    [progress publish];
    dispatch_async(dispatch_get_main_queue(), ^{
      trackCopy(session);
    });
  }

  bool ok = false;
  {
    std::unique_lock lock(session->mutex);
    session->changed.wait_for(lock, kPromiseTimeout, [&] { return session->ready[i] || session->failed; });
    ok = session->ready[i] && !session->failed;
    session->progress[i] = nil;
  }
  if (progress != nil) {
    [progress unpublish];
  }

  NSError *error = nil;
  NSURL *src = [NSURL fileURLWithPath:item.path];
  if (ok && progress != nil) {
    // swap the placeholder (always a file) for the real item; across volumes, copy
    [fm removeItemAtURL:url error:nil];
    if (![fm moveItemAtURL:src toURL:url error:&error]) {
      error = nil;
      if (![fm copyItemAtURL:src toURL:url error:&error]) {
        ok = false;
      }
    }
  } else if (ok) {
    if (![fm moveItemAtURL:src toURL:url error:&error]) {
      error = nil;
      if (![fm copyItemAtURL:src toURL:url error:&error]) {
        ok = false;
      }
    }
  } else {
    if (progress != nil) {
      [fm removeItemAtURL:url error:nil];
    }
    error = [NSError errorWithDomain:NSCocoaErrorDomain code:NSUserCancelledError userInfo:nil];
  }

  if (progress != nil) {
    {
      std::scoped_lock lock(session->mutex);
      session->settled[i] = true;
    }
    refreshCopyWindowSoon();
  }

  completionHandler(ok ? nil : error);
  emitLog(
      ok ? @"info" : @"warn",
      [NSString stringWithFormat:@"file drag: %@ \"%@\"", ok ? @"delivered" : @"failed to deliver", url.path]
  );
  emit(@{@"evt" : @"delivered", @"id" : @(session->transferId), @"index" : @(i), @"ok" : @(ok)});

  bool allDone = false;
  {
    std::scoped_lock lock(session->mutex);
    session->delivered.at(i) = true;
    allDone = std::all_of(session->delivered.begin(), session->delivered.end(), [](bool d) { return d; });
  }
  if (allDone) {
    removeSession(session->transferId);
  }
}

@end

namespace {

// main thread only
NSPanel *s_panel = nil;
DFDragView *s_view = nil;
NSTimer *s_followTimer = nil;

// Keep the panel centred under the cursor until the press reaches it: on a
// server the user moves the real cursor while the drag is being set up.
void startFollowingCursor()
{
  [s_followTimer invalidate];
  const CFAbsoluteTime until = CFAbsoluteTimeGetCurrent() + 3.0;
  s_followTimer = [NSTimer
      scheduledTimerWithTimeInterval:1.0 / 120.0
                             repeats:YES
                               block:^(NSTimer *timer) {
                                 if (!s_panel.visible || CFAbsoluteTimeGetCurrent() > until) {
                                   [timer invalidate];
                                   return;
                                 }
                                 const NSPoint p = NSEvent.mouseLocation;
                                 const NSSize size = s_panel.frame.size;
                                 [s_panel setFrameOrigin:NSMakePoint(p.x - size.width / 2, p.y - size.height / 2)];
                               }];
}

// any thread: every drag whose files are not all delivered yet
std::mutex s_sessionMutex;
std::map<uint32_t, std::shared_ptr<Session>> s_sessions;

std::shared_ptr<Session> findSession(uint32_t transferId)
{
  std::scoped_lock lock(s_sessionMutex);
  const auto it = s_sessions.find(transferId);
  return it == s_sessions.end() ? nullptr : it->second;
}

void removeSession(uint32_t transferId)
{
  std::scoped_lock lock(s_sessionMutex);
  s_sessions.erase(transferId);
}

void ensurePanel()
{
  if (s_panel != nil) {
    return;
  }
  // big enough to stay under a moving cursor between two follow updates
  const CGFloat size = 96;
  s_panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, size, size)
                                       styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                         backing:NSBackingStoreBuffered
                                           defer:NO];
  s_panel.becomesKeyOnlyIfNeeded = YES;
  s_panel.floatingPanel = YES;
  s_panel.hidesOnDeactivate = NO;
  s_panel.level = NSPopUpMenuWindowLevel;
  s_panel.opaque = NO;
  s_panel.backgroundColor = [NSColor clearColor];
  s_panel.hasShadow = NO;
  s_panel.ignoresMouseEvents = NO;
  s_panel.releasedWhenClosed = NO;
  s_panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorTransient;

  s_view = [[DFDragView alloc] initWithFrame:NSMakeRect(0, 0, size, size)];
  s_view.writeQueue = [[NSOperationQueue alloc] init];
  s_view.writeQueue.qualityOfService = NSQualityOfServiceUserInitiated;
  s_panel.contentView = s_view;
}

void begin(NSDictionary *msg)
{
  auto session = std::make_shared<Session>();
  session->transferId = [msg[@"id"] unsignedIntValue];
  for (NSDictionary *item in msg[@"items"]) {
    session->items.push_back(
        {item[@"name"], item[@"path"], [item[@"isDir"] boolValue], [item[@"size"] unsignedLongLongValue],
         std::max(1u, [item[@"count"] unsignedIntValue])}
    );
  }
  session->ready.assign(session->items.size(), false);
  session->bytes.assign(session->items.size(), 0);
  session->progress.assign(session->items.size(), nil);
  session->placeholder.assign(session->items.size(), false);
  session->settled.assign(session->items.size(), false);
  session->delivered.assign(session->items.size(), false);
  const double x = [msg[@"x"] doubleValue];
  const double y = [msg[@"y"] doubleValue];

  {
    // earlier drops keep receiving; the core aborts a drag it replaces
    std::scoped_lock lock(s_sessionMutex);
    s_sessions[session->transferId] = session;
  }

  dispatch_async(dispatch_get_main_queue(), ^{
    ensurePanel();
    s_view.session = session;

    // global (top-left origin) -> Cocoa (bottom-left origin of the main screen)
    const CGFloat mainHeight = NSScreen.screens.firstObject.frame.size.height;
    const NSSize size = s_panel.frame.size;
    [s_panel setFrameOrigin:NSMakePoint(x - size.width / 2, mainHeight - y - size.height / 2)];
    [s_panel orderFrontRegardless];
    startFollowingCursor();

    // give the window server a moment to put the panel on screen
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 80 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
      emit(@{@"evt" : @"ready", @"id" : @(session->transferId)});
    });
  });
}

void pressAt(NSDictionary *msg)
{
  auto session = findSession([msg[@"id"] unsignedIntValue]);
  if (!session) {
    return;
  }
  const double x = [msg[@"x"] doubleValue];
  const double y = [msg[@"y"] doubleValue];
  dispatch_async(dispatch_get_main_queue(), ^{
    // stop following: the press must find the panel exactly where it is aimed
    [s_followTimer invalidate];
    s_followTimer = nil;
    const CGFloat mainHeight = NSScreen.screens.firstObject.frame.size.height;
    const NSSize size = s_panel.frame.size;
    [s_panel setFrameOrigin:NSMakePoint(x - size.width / 2, mainHeight - y - size.height / 2)];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
      emit(@{@"evt" : @"placed", @"id" : @(session->transferId)});
    });
  });
}

void itemProgress(NSDictionary *msg)
{
  if (auto session = findSession([msg[@"id"] unsignedIntValue])) {
    const size_t index = [msg[@"index"] unsignedLongValue];
    std::scoped_lock lock(session->mutex);
    if (index < session->bytes.size()) {
      session->bytes[index] = [msg[@"bytes"] unsignedLongLongValue];
      if (session->progress[index] != nil) {
        session->progress[index].completedUnitCount = static_cast<int64_t>(session->bytes[index]);
      }
    }
  }
  refreshCopyWindowSoon();
}

void itemReady(NSDictionary *msg)
{
  if (auto session = findSession([msg[@"id"] unsignedIntValue])) {
    const size_t index = [msg[@"index"] unsignedLongValue];
    std::scoped_lock lock(session->mutex);
    if (index < session->ready.size()) {
      session->ready[index] = true;
    }
    session->changed.notify_all();
  }
}

void abortSession(NSDictionary *msg)
{
  auto session = findSession([msg[@"id"] unsignedIntValue]);
  if (!session) {
    return;
  }
  {
    std::scoped_lock lock(session->mutex);
    session->failed = true;
    session->changed.notify_all();
  }
  removeSession(session->transferId);
  dispatch_async(dispatch_get_main_queue(), ^{
    if (s_view.session == session && !session->dragStarted) {
      [s_panel orderOut:nil];
      s_view.session = nullptr;
    }
  });
}

void readCommands()
{
  char *line = nullptr;
  size_t capacity = 0;
  while (getline(&line, &capacity, stdin) > 0) {
    @autoreleasepool {
      NSData *data = [NSData dataWithBytes:line length:strlen(line)];
      NSDictionary *msg = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
      if (![msg isKindOfClass:[NSDictionary class]]) {
        continue;
      }
      NSString *cmd = msg[@"cmd"];
      if ([cmd isEqualToString:@"begin"]) {
        begin(msg);
      } else if ([cmd isEqualToString:@"pressAt"]) {
        pressAt(msg);
      } else if ([cmd isEqualToString:@"progress"]) {
        itemProgress(msg);
      } else if ([cmd isEqualToString:@"itemReady"]) {
        itemReady(msg);
      } else if ([cmd isEqualToString:@"abort"]) {
        abortSession(msg);
      }
    }
  }
  free(line);
  // stdin closed: deskflow-core went away
  exit(0);
}

} // namespace

int main(int, char **)
{
  @autoreleasepool {
    [NSApplication sharedApplication];
    // no Dock icon, never activated by our own panel
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

    [NSThread detachNewThreadWithBlock:^{
      readCommands();
    }];
    emit(@{@"evt" : @"hello"});
    [NSApp run];
  }
  return 0;
}
