// gesture-replay: replays recorded gesture CGEvents (from gesture-probe) and reports the
// active Space, to find out which events and fields macOS needs for a synthetic gesture.
// Driven by /tmp/gesture-replay.plan, one command per line:
//   space                                   print the active Space id
//   wait <ms>
//   play <file.jsonl> <hid|session|annotated> [skip=f1,f2] [set=f:v,...] [scale=x] [types=29,30]
// Output goes to /tmp/gesture-replay.out.
#import <ApplicationServices/ApplicationServices.h>
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#include <cmath>
#include <set>
#include <map>
#include <vector>
#include <mach/mach_time.h>
#include "platform/OSXTrackpadGesture.h"

typedef int CGSConnectionID;
extern "C" CGSConnectionID CGSMainConnectionID(void);
extern "C" uint64_t CGSGetActiveSpace(CGSConnectionID cid);


// --- macOS 27: the Dock reads an IOHID gesture record embedded in the serialized
// event (tag 4205), not the Quartz fields. Technique from InstantSpaceSwitcher (MIT).
#pragma pack(push, 1)
struct HIDEventBase { uint32_t size, type, options; uint8_t depth, reserved[3]; };
struct HIDFluidGesture { HIDEventBase base; int32_t px, py, pz; uint32_t swipeMask; uint16_t motion, flavor; int32_t progress; };
struct HIDVelocity { HIDEventBase base; int32_t vx, vy, vz; };
struct HIDQueueHeader { uint64_t timestamp, senderId; uint32_t options, attributeLength, eventCount; };
#pragma pack(pop)
static int32_t fixed1616(double v) { int32_t f = (int32_t)(v * 65536.0); return (f == 0 && v != 0) ? (v > 0 ? 1 : -1) : f; }
static CGEventRef augmentDockSwipe(CGEventRef ev)
{
  CFDataRef data = CGEventCreateData(kCFAllocatorDefault, ev);
  if (!data) return NULL;
  const uint8_t *bytes = CFDataGetBytePtr(data);
  CFIndex len = CFDataGetLength(data);
  if (len < 4 || bytes[3] != 2) { CFRelease(data); return NULL; }
  int64_t phase = CGEventGetIntegerValueField(ev, (CGEventField)132);
  double vx = CGEventGetDoubleValueField(ev, (CGEventField)129), vy = CGEventGetDoubleValueField(ev, (CGEventField)130);
  bool withVelocity = vx != 0 || vy != 0 || phase == 4;
  size_t plen = sizeof(HIDQueueHeader) + sizeof(HIDFluidGesture) + (withVelocity ? sizeof(HIDVelocity) : 0);
  std::vector<uint8_t> out(bytes, bytes + len);
  out.push_back((uint8_t)(plen >> 8)); out.push_back((uint8_t)plen);
  out.push_back((uint8_t)(4205 >> 8)); out.push_back((uint8_t)(4205 & 0xff));
  size_t at = out.size(); out.resize(at + plen, 0);
  auto *h = (HIDQueueHeader *)&out[at];
  h->timestamp = CGEventGetTimestamp(ev) ? CGEventGetTimestamp(ev) : mach_absolute_time();
  h->eventCount = withVelocity ? 2 : 1;
  auto *g = (HIDFluidGesture *)&out[at + sizeof(HIDQueueHeader)];
  g->base.size = sizeof(HIDFluidGesture); g->base.type = 23; g->base.options = (uint32_t)((phase & 0xff) << 24);
  g->px = fixed1616(CGEventGetDoubleValueField(ev, (CGEventField)125));
  g->py = fixed1616(CGEventGetDoubleValueField(ev, (CGEventField)126));
  g->swipeMask = (uint32_t)CGEventGetIntegerValueField(ev, (CGEventField)115);
  g->motion = (uint16_t)CGEventGetIntegerValueField(ev, (CGEventField)123);
  g->flavor = 3;
  g->progress = fixed1616(CGEventGetDoubleValueField(ev, (CGEventField)124));
  if (withVelocity) {
    auto *v = (HIDVelocity *)&out[at + sizeof(HIDQueueHeader) + sizeof(HIDFluidGesture)];
    v->base.size = sizeof(HIDVelocity); v->base.type = 9; v->base.depth = 1;
    v->vx = fixed1616(vx); v->vy = fixed1616(vy);
  }
  CFRelease(data);
  CFDataRef nd = CFDataCreate(kCFAllocatorDefault, out.data(), (CFIndex)out.size());
  CGEventRef res = CGEventCreateFromData(kCFAllocatorDefault, nd);
  CFRelease(nd);
  return res;
}
static void postCompanion(CGEventTapLocation tap)
{
  CGEventRef c = CGEventCreate(NULL);
  CGEventSetIntegerValueField(c, (CGEventField)55, 29);
  CGEventPost(tap, c);
  CFRelease(c);
}
// minimal swipe: began/changed/ended, like InstantSpaceSwitcher. sign: +1/-1
static void miniSwipe(CGEventTapLocation tap, double sign, bool augment)
{
  int phases[] = {1, 2, 4};
  for (int ph : phases) {
    CGEventRef ev = CGEventCreate(NULL);
    CGEventSetIntegerValueField(ev, (CGEventField)55, 30);
    CGEventSetIntegerValueField(ev, (CGEventField)110, 23);
    CGEventSetIntegerValueField(ev, (CGEventField)132, ph);
    CGEventSetIntegerValueField(ev, (CGEventField)134, ph);
    CGEventSetIntegerValueField(ev, (CGEventField)123, 1);
    CGEventSetDoubleValueField(ev, (CGEventField)124, sign * 0.000016);
    CGEventSetDoubleValueField(ev, (CGEventField)138, 3.0);
    CGEventSetDoubleValueField(ev, (CGEventField)169, (double)mach_absolute_time());
    CGEventSetDoubleValueField(ev, (CGEventField)125, 0.1);
    if (ph == 4) CGEventSetDoubleValueField(ev, (CGEventField)129, sign * 9999.0);
    CGEventRef post = augment ? augmentDockSwipe(ev) : (CGEventRef)CFRetain(ev);
    CFRelease(ev);
    if (post) { CGEventPost(tap, post); CFRelease(post); }
    postCompanion(tap);
  }
}

static uint64_t activeSpace() { return CGSGetActiveSpace(CGSMainConnectionID()); }

extern "C" CFArrayRef CGSCopyManagedDisplaySpaces(CGSConnectionID cid);

// index of the active Space among the user Spaces of its display, -1 if unknown
static int activeSpaceIndex(int *count)
{
  CGSConnectionID cid = CGSMainConnectionID();
  uint64_t active = CGSGetActiveSpace(cid);
  NSArray *displays = (__bridge_transfer NSArray *)CGSCopyManagedDisplaySpaces(cid);
  for (NSDictionary *d in displays) {
    NSArray *spaces = d[@"Spaces"];
    for (NSUInteger i = 0; i < spaces.count; ++i) {
      if ([spaces[i][@"ManagedSpaceID"] unsignedLongLongValue] == active) {
        if (count) *count = (int)spaces.count;
        return (int)i;
      }
    }
  }
  return -1;
}

static std::set<int> parseIntSet(NSString *s)
{
  std::set<int> out;
  for (NSString *p in [s componentsSeparatedByString:@","])
    if (p.length) out.insert(p.intValue);
  return out;
}

static void play(NSArray<NSString *> *args)
{
  NSString *file = args[1];
  NSString *tapName = args.count > 2 ? args[2] : @"hid";
  CGEventTapLocation tap = [tapName isEqual:@"session"] ? kCGSessionEventTap
                         : [tapName isEqual:@"annotated"] ? kCGAnnotatedSessionEventTap : kCGHIDEventTap;
  std::set<int> skip = {39, 40, 58}; // target app, timestamp
  std::set<int> types;
  std::map<int, double> overrides;
  double scale = 1;
  bool augment = false, companion = false;
  for (NSUInteger i = 3; i < args.count; ++i) {
    NSString *a = args[i];
    if ([a hasPrefix:@"skip="]) { auto s = parseIntSet([a substringFromIndex:5]); skip.insert(s.begin(), s.end()); }
    else if ([a hasPrefix:@"types="]) types = parseIntSet([a substringFromIndex:6]);
    else if ([a isEqual:@"augment"]) augment = true;
    else if ([a isEqual:@"companion"]) companion = true;
    else if ([a hasPrefix:@"scale="]) scale = [a substringFromIndex:6].doubleValue;
    else if ([a hasPrefix:@"set="]) {
      for (NSString *kv in [[a substringFromIndex:4] componentsSeparatedByString:@","]) {
        NSArray *p = [kv componentsSeparatedByString:@":"];
        if (p.count == 2) overrides[[p[0] intValue]] = [p[1] doubleValue];
      }
    }
  }
  NSString *text = [NSString stringWithContentsOfFile:file encoding:NSUTF8StringEncoding error:nil];
  if (!text) { printf("cannot read %s\n", file.UTF8String); return; }
  double lastT = -1;
  int posted = 0;
  for (NSString *line in [text componentsSeparatedByString:@"\n"]) {
    if (!line.length) continue;
    NSDictionary *ev = [NSJSONSerialization JSONObjectWithData:[line dataUsingEncoding:NSUTF8StringEncoding] options:0 error:nil];
    if (!ev) continue;
    int type = [ev[@"type"] intValue];
    if (!types.empty() && !types.count(type)) continue;
    double t = [ev[@"t"] doubleValue];
    if (lastT >= 0 && t > lastT) usleep((useconds_t)((t - lastT) * scale * 1e6));
    lastT = t;
    CGEventRef e = CGEventCreate(NULL);
    CGEventSetType(e, (CGEventType)type);
    NSDictionary *fields = ev[@"f"];
    for (NSString *key in fields) {
      int f = key.intValue;
      if (skip.count(f) || f == 55) continue;
      NSArray *v = fields[key];
      long long iv = [v[0] longLongValue];
      id dvObj = v[1];
      double dv = [dvObj isKindOfClass:[NSNumber class]] ? [dvObj doubleValue] : NAN;
      if (!std::isnan(dv) && (double)iv != dv && std::fabs(dv - (double)iv) > 1e-3 * std::fmax(1.0, std::fabs(dv)))
        CGEventSetDoubleValueField(e, (CGEventField)f, dv);
      else
        CGEventSetIntegerValueField(e, (CGEventField)f, iv);
    }
    for (auto &[f, v] : overrides) {
      if (v == std::floor(v)) CGEventSetIntegerValueField(e, (CGEventField)f, (int64_t)v);
      else CGEventSetDoubleValueField(e, (CGEventField)f, v);
    }
    if (augment && type == 30) {
      CGEventRef a = augmentDockSwipe(e);
      if (a) { CFRelease(e); e = a; }
    }
    CGEventPost(tap, e);
    CFRelease(e);
    if (companion && type == 30) postCompanion(tap);
    ++posted;
  }
  printf("  posted %d events\n", posted);
}


// Deskflow pipeline on a raw recording: Forwarder::take (server) picks and
// serializes, post (client) replays. deskflow <file> [from=t] [to=t]
static void playDeskflow(NSArray<NSString *> *args)
{
  NSString *text = [NSString stringWithContentsOfFile:args[1] encoding:NSUTF8StringEncoding error:nil];
  double from = -1, to = 1e9;
  for (NSUInteger i = 2; i < args.count; ++i) {
    if ([args[i] hasPrefix:@"from="]) from = [args[i] substringFromIndex:5].doubleValue;
    else if ([args[i] hasPrefix:@"to="]) to = [args[i] substringFromIndex:3].doubleValue;
  }
  deskflow::osx::gesture::Forwarder forwarder;
  double lastT = -1; int seen = 0, forwarded = 0;
  for (NSString *line in [text componentsSeparatedByString:@"\n"]) {
    if (!line.length) continue;
    NSDictionary *ev = [NSJSONSerialization JSONObjectWithData:[line dataUsingEncoding:NSUTF8StringEncoding] options:0 error:nil];
    double t = [ev[@"t"] doubleValue];
    if (t < from || t > to) continue;
    NSData *d = [[NSData alloc] initWithBase64EncodedString:ev[@"d"] options:0];
    CGEventRef e = CGEventCreateFromData(kCFAllocatorDefault, (__bridge CFDataRef)d);
    if (!e) continue;
    ++seen;
    if (lastT >= 0 && t > lastT) usleep((useconds_t)((t - lastT) * 1e6));
    lastT = t;
    // Use the production finger detector, including on reconstructed events.
    auto out = forwarder.take((CGEventType)[ev[@"type"] intValue], e);
    CFRelease(e);
    for (auto &bytes : out) { deskflow::osx::gesture::post(bytes); ++forwarded; }
  }
  printf("  forwarded %d of %d events\n", forwarded, seen);
}

// --- serialized recording / replay for gesture diagnostics ---
static FILE *g_rawOut;
static CFAbsoluteTime g_rawStart;
static CFMachPortRef g_rawTap;
static CGEventRef rawTapCallback(CGEventTapProxy, CGEventType type, CGEventRef event, void *)
{
  if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
    CGEventTapEnable(g_rawTap, true);
    return event;
  }
  const int t = static_cast<int>(type);
  bool want = t == 29 || t == 30;
  if (t == kCGEventScrollWheel) {
    want = CGEventGetIntegerValueField(event, kCGScrollWheelEventScrollPhase) != 0 ||
           CGEventGetIntegerValueField(event, kCGScrollWheelEventMomentumPhase) != 0;
  }
  if (!want) return event;
  CFDataRef data = CGEventCreateData(kCFAllocatorDefault, event);
  if (!data) return event;
  NSString *b64 = [(__bridge NSData *)data base64EncodedStringWithOptions:0];
  fprintf(g_rawOut, "{\"t\":%.4f,\"type\":%d,\"hid\":%lld,\"phase\":%lld,\"len\":%ld,\"d\":\"%s\"}\n",
          CFAbsoluteTimeGetCurrent() - g_rawStart, t,
          CGEventGetIntegerValueField(event, static_cast<CGEventField>(110)),
          CGEventGetIntegerValueField(event, static_cast<CGEventField>(132)),
          static_cast<long>(CFDataGetLength(data)), b64.UTF8String);
  CFRelease(data);
  return event;
}

static void recordRaw(double seconds, NSString *file)
{
  g_rawOut = fopen(file.UTF8String, "w");
  if (!g_rawOut) { printf("  cannot open recording file\n"); return; }
  g_rawStart = CFAbsoluteTimeGetCurrent();
  g_rawTap = CGEventTapCreate(kCGHIDEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly,
                            kCGEventMaskForAllEvents, rawTapCallback, nullptr);
  if (!g_rawTap) { printf("  cannot create tap\n"); fclose(g_rawOut); return; }
  CFRunLoopSourceRef src = CFMachPortCreateRunLoopSource(nullptr, g_rawTap, 0);
  CFRunLoopAddSource(CFRunLoopGetCurrent(), src, kCFRunLoopCommonModes);
  CGEventTapEnable(g_rawTap, true);
  printf("  recording %.0f s\n", seconds);
  CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, false);
  CGEventTapEnable(g_rawTap, false);
  CFRunLoopRemoveSource(CFRunLoopGetCurrent(), src, kCFRunLoopCommonModes);
  CFRelease(src);
  CFRelease(g_rawTap);
  g_rawTap = nullptr;
  fclose(g_rawOut);
  printf("  recorded\n");
}

// replayraw <file> <hid|session> [from=t] [to=t] [types=29,30] [restamp]
static void replayRaw(NSArray<NSString *> *args)
{
  NSString *text = [NSString stringWithContentsOfFile:args[1] encoding:NSUTF8StringEncoding error:nil];
  CGEventTapLocation tap = (args.count > 2 && [args[2] isEqual:@"session"]) ? kCGSessionEventTap : kCGHIDEventTap;
  double from = -1, to = 1e9;
  std::set<int> types;
  bool restamp = false;
  for (NSUInteger i = 3; i < args.count; ++i) {
    NSString *a = args[i];
    if ([a hasPrefix:@"from="]) from = [a substringFromIndex:5].doubleValue;
    else if ([a hasPrefix:@"to="]) to = [a substringFromIndex:3].doubleValue;
    else if ([a hasPrefix:@"types="]) types = parseIntSet([a substringFromIndex:6]);
    else if ([a isEqual:@"restamp"]) restamp = true;
  }
  double lastT = -1;
  int posted = 0;
  for (NSString *line in [text componentsSeparatedByString:@"\n"]) {
    if (!line.length) continue;
    NSDictionary *ev = [NSJSONSerialization JSONObjectWithData:[line dataUsingEncoding:NSUTF8StringEncoding] options:0 error:nil];
    if (ev[@"tap"] && ![ev[@"tap"] isEqual:@"hid"]) continue;
    const double t = [ev[@"t"] doubleValue];
    const int type = [ev[@"type"] intValue];
    if (t < from || t > to || (!types.empty() && !types.count(type))) continue;
    NSData *d = [[NSData alloc] initWithBase64EncodedString:ev[@"d"] options:0];
    CGEventRef e = CGEventCreateFromData(kCFAllocatorDefault, (__bridge CFDataRef)d);
    if (!e) continue;
    if (lastT >= 0 && t > lastT) usleep(static_cast<useconds_t>((t - lastT) * 1e6));
    lastT = t;
    if (restamp) CGEventSetTimestamp(e, clock_gettime_nsec_np(CLOCK_UPTIME_RAW));
    CGEventPost(tap, e);
    CFRelease(e);
    ++posted;
  }
  printf("  posted %d raw events\n", posted);
}

int main(int argc, const char **argv)
{
  @autoreleasepool {
    if (isatty(1) == 0) {
      freopen("/tmp/gesture-replay.out", "w", stdout);
      setvbuf(stdout, NULL, _IOLBF, 0);
    }
    if (!CGPreflightPostEventAccess()) {
      CGRequestPostEventAccess();
      printf("no Accessibility permission yet\n");
      return 2;
    }
    NSString *plan = [NSString stringWithContentsOfFile:@"/tmp/gesture-replay.plan" encoding:NSUTF8StringEncoding error:nil];
    for (NSString *raw in [plan componentsSeparatedByString:@"\n"]) {
      NSString *line = [raw stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
      if (!line.length || [line hasPrefix:@"#"]) continue;
      // "@n cmd": run cmd only if the active Space is at index n (avoids
      // swiping past the first or last Space after a failed switch)
      if ([line hasPrefix:@"@"]) {
        NSRange sp = [line rangeOfString:@" "];
        int want = [[line substringWithRange:NSMakeRange(1, sp.location - 1)] intValue];
        int count = 0;
        int have = activeSpaceIndex(&count);
        if (have != want) {
          printf("> skip (space index %d, wanted %d): %s\n", have, want, line.UTF8String);
          continue;
        }
        line = [line substringFromIndex:sp.location + 1];
      }
      NSArray<NSString *> *args = [line componentsSeparatedByString:@" "];
      printf("> %s\n", line.UTF8String);
      if ([args[0] isEqual:@"space"]) {
        int count = 0;
        int idx = activeSpaceIndex(&count);
        printf("  space %llu (index %d of %d)\n", activeSpace(), idx, count);
      }
      else if ([args[0] isEqual:@"wait"]) usleep((useconds_t)(args[1].intValue * 1000));
      else if ([args[0] isEqual:@"play"] && args.count > 1) play(args);
      else if ([args[0] isEqual:@"deskflow"] && args.count > 1) playDeskflow(args);
      else if ([args[0] isEqual:@"record"] && args.count > 2) recordRaw(args[1].doubleValue, args[2]);
      else if ([args[0] isEqual:@"replayraw"] && args.count > 1) replayRaw(args);
      else if ([args[0] isEqual:@"key"] && args.count > 1) {
        CGKeyCode k = (CGKeyCode)args[1].intValue;
        for (bool down : {true, false}) {
          CGEventRef e = CGEventCreateKeyboardEvent(NULL, k, down);
          CGEventPost(kCGHIDEventTap, e);
          CFRelease(e);
          usleep(20000);
        }
      }
      else if ([args[0] isEqual:@"mini"] && args.count > 1) {
        CGEventTapLocation tap = (args.count > 2 && [args[2] isEqual:@"hid"]) ? kCGHIDEventTap : kCGSessionEventTap;
        bool aug = !(args.count > 3 && [args[3] isEqual:@"plain"]);
        miniSwipe(tap, args[1].doubleValue, aug);
      }
    }
    printf("done\n");
  }
  return 0;
}
