// gesture-probe: dumps the raw fields of trackpad gesture CGEvents.
// Usage: gesture-probe [seconds]   (needs Input Monitoring permission)
#import <ApplicationServices/ApplicationServices.h>
#import <Foundation/Foundation.h>

static CFAbsoluteTime g_start;

static CGEventRef tapCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *refcon)
{
  if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
    CGEventTapEnable(*(CFMachPortRef *)refcon, true);
    return event;
  }
  int t = (int)type;
  bool interesting = t == 29 || t == 30 || t == 31 || t == 18 || t == 19 || t == 20 || t == 32;
  if (t == kCGEventScrollWheel) {
    // only phased (trackpad) scrolls
    interesting = CGEventGetIntegerValueField(event, kCGScrollWheelEventScrollPhase) != 0 ||
                  CGEventGetIntegerValueField(event, kCGScrollWheelEventMomentumPhase) != 0;
  }
  if (!interesting)
    return event;

  NSMutableString *line = [NSMutableString stringWithFormat:@"{\"t\":%.4f,\"type\":%d,\"f\":{", CFAbsoluteTimeGetCurrent() - g_start, t];
  bool first = true;
  for (int f = 0; f < 256; ++f) {
    int64_t iv = CGEventGetIntegerValueField(event, (CGEventField)f);
    double dv = CGEventGetDoubleValueField(event, (CGEventField)f);
    if (iv == 0 && dv == 0.0)
      continue;
    [line appendFormat:@"%s\"%d\":[%lld,%.6g]", first ? "" : ",", f, iv, dv];
    first = false;
  }
  [line appendString:@"}}\n"];
  fputs(line.UTF8String, stdout);
  fflush(stdout);
  return event;
}

int main(int argc, const char **argv)
{
  @autoreleasepool {
    // when launched as an app bundle, log next to /tmp
    if (getenv("GESTURE_PROBE_OUT") == NULL && isatty(1) == 0 && argc == 1) {
      freopen("/tmp/gesture-probe.jsonl", "w", stdout);
      freopen("/tmp/gesture-probe.err", "w", stderr);
    }
    double seconds = argc > 1 ? atof(argv[1]) : 120;
    if (!CGPreflightListenEventAccess()) {
      CGRequestListenEventAccess(); // shows the Input Monitoring prompt
      fprintf(stderr, "no Input Monitoring permission yet: grant it, then relaunch\n");
      return 2;
    }
    static CFMachPortRef tap;
    tap = CGEventTapCreate(kCGHIDEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly, kCGEventMaskForAllEvents, tapCallback, &tap);
    if (!tap) {
      fprintf(stderr, "failed to create event tap (grant Input Monitoring)\n");
      return 1;
    }
    CFRunLoopSourceRef src = CFMachPortCreateRunLoopSource(NULL, tap, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), src, kCFRunLoopCommonModes);
    CGEventTapEnable(tap, true);
    g_start = CFAbsoluteTimeGetCurrent();
    fprintf(stderr, "recording for %.0f s\n", seconds);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, false);
  }
  return 0;
}
