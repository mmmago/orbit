<!-- SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux -->
<!-- SPDX-License-Identifier: MIT -->
<!-- Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md. -->

# macOS server cursor hiding

Upstream's [issue #10225](https://github.com/deskflow/deskflow/issues/10225)
tracks the server cursor remaining visible when crossing an edge containing
the menu bar or Dock. Deskflow 1.27 stopped warping the server cursor back to
the center and instead disconnects physical mouse motion from its position.
This fork inherited that code; its file transfer, clipboard and trackpad
changes did not replace the cursor hiding mechanism.

An already visible menu bar may work normally. Revealing an auto-hidden menu
bar during departure can override the hide asynchronously, especially when
leaving a native fullscreen app. See the investigation in
[PR #10184](https://github.com/deskflow/deskflow/pull/10184). That PR was closed;
it used an undocumented menu-bar notification to reapply hiding.

## Experimental workaround in this fork

The server still hides and captures the cursor immediately. For the first
second after departure it reasserts hiding every 100 ms, using a balanced
`CGDisplayShowCursor` / `CGDisplayHideCursor` pair. It does not call the normal
show/hide helpers, which reconnect mouse motion and would undo capture.
Apple documents the need to balance requests in
[Controlling the Mouse Cursor](https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/QuartzDisplayServicesConceptual/Articles/MouseCursor.html).

Retries stop on return, shutdown, or a CoreGraphics failure. They run on the
same event queue as screen transitions; returning quickly cannot leave a
delayed hide running afterwards. Accessibility permission loss posts Quit
so cleanup also runs on that thread. Clients do not run the retries.

This is a bounded timing workaround using public cursor calls, not a confirmed
fix for every menu bar/Dock case. A reveal or another application overriding
cursor state after the retry window could still cause a ghost cursor. The
cursor may briefly appear before a retry. `CGCursorIsVisible()` is not a
reliable visual check for this bug: affected setups can report hidden while
showing a cursor.

## Manual verification

Use the new build on the Mac server; a client update is not required for this
change. With the client above the server and the menu bar set to hide in
fullscreen:

1. Put a browser in native fullscreen. Cross the top edge both slowly and
   quickly, including after lingering at the top to reveal the menu bar.
   Check the physical server display for a ghost cursor while moving on the
   client, including after waiting more than one second.
2. Repeat on the desktop and with a windowed browser, and through other edges
   (including the Dock edge).
3. Return in less than 100 ms, between retries, and after several seconds.
   The cursor should appear immediately and remain visible on the server.
4. Disconnect or stop the server while controlling the client. The local
   cursor should reappear and track the mouse normally.
5. Repeat with file drags to/from the server and trackpad gestures. Neither
   input capture nor drag handover should change.

Debug logs include `reapplying cursor hiding after departure` for each retry.
Compilation and automated regression tests do not verify the visual fullscreen
behavior; it requires the two-machine check above.
