// gesture-target: a window that logs the gesture events it receives (test helper)
#import <Cocoa/Cocoa.h>
static FILE *g_log;
int main(int argc, const char **argv) {
  @autoreleasepool {
    g_log = fopen("/tmp/gesture-target.log", "w"); setvbuf(g_log, NULL, _IOLBF, 0);
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    NSRect f = NSMakeRect(0, 0, 700, 450);
    NSWindow *w = [[NSWindow alloc] initWithContentRect:f styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    w.title = @"Gesture target (test window, closes by itself)";
    [w center]; [w makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskAny handler:^NSEvent *(NSEvent *e) {
      switch (e.type) {
        case NSEventTypeMagnify: fprintf(g_log, "magnify phase=%lu m=%.4f\n", (unsigned long)e.phase, e.magnification); break;
        case NSEventTypeRotate: fprintf(g_log, "rotate phase=%lu r=%.3f\n", (unsigned long)e.phase, e.rotation); break;
        case NSEventTypeGesture: case NSEventTypeBeginGesture: case NSEventTypeEndGesture: case NSEventTypeSwipe: case NSEventTypeSmartMagnify:
          fprintf(g_log, "gesture-type %lu\n", (unsigned long)e.type); break;
        default: break;
      }
      return e;
    }];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.6 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
      NSRect r = w.frame; CGFloat h = NSScreen.screens[0].frame.size.height;
      CGWarpMouseCursorPosition(CGPointMake(NSMidX(r), h - NSMidY(r)));
      fprintf(g_log, "ready\n");
    });
    double secs = argc > 1 ? atof(argv[1]) : 8;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(secs * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ fprintf(g_log, "bye\n"); [NSApp terminate:nil]; });
    [NSApp run];
  }
  return 0;
}
