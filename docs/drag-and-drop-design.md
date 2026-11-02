<!-- SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux -->
<!-- SPDX-License-Identifier: MIT -->
<!-- Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md. -->

# Drag-and-drop file transfer for Deskflow (Universal Control style)

Status: implemented for macOS ↔ macOS in this fork (both directions, files
and folders, Finder-style progress, parallel transfers). Upstream declined
file transfer (deskflow/deskflow#9648, #8104), so this lives in a fork. The
sections below are kept in the order they were written: the original plan,
the spike results, then what was actually built and changed in each round.

## Goal

Grab one or more files/folders on one computer, drag the cursor across the
screen edge, and drop them on the other computer — into Finder, the Desktop,
or any app that accepts file drops — exactly like Universal Control.

First target: macOS ↔ macOS (one Mac as server, another as client).
Windows/Linux later, behind the same protocol.

## What exists today

- `kMsgDDragInfo` (`DDRG`) and `kMsgDFileTransfer` (`DFTR`) are still defined
  in `ProtocolTypes.h/.cpp`, marked deprecated. `ClientProxy1_5` parses them
  into empty stubs. `sendDragInfo` is a no-op everywhere.
- `OSXPasteboardPeeker.m` can read file paths from the drag pasteboard.
- `OSXCocoaApp.m` (`runCocoaApp`) is dead code: nothing calls it.
- `Computer::isLockedToComputer()` returns true whenever any mouse button is
  held, so today the cursor **cannot** cross an edge mid-drag at all.
- Clipboard transfer is the closest model: `StreamChunker` splits data into
  512 KB `ClipboardChunk`s queued as events on the same TCP/TLS stream.

The old `DDRG`/`DFTR` formats are too limited (no transfer ID, one file at a
time, no cancel, no folder structure), so we add new messages instead of
reviving them.

## User flow (server → client; client → server is symmetric)

1. User presses the mouse on a file in Finder on the server Mac and starts dragging.
2. Cursor reaches the edge with the button held.
3. Server checks the **drag pasteboard**: if its `changeCount` moved since the
   mouse-down and it holds `public.file-url` items, this is a file drag →
   allow the switch (instead of "locked by mouse button").
4. Server sends `DragBegin` (file list + sizes) to the client, switches screens,
   and cancels the local drag on the server Mac.
5. Client starts a native drag session under its cursor using **file
   promises** (`NSFilePromiseProvider`), so the client Mac shows the normal drag
   image and drop targets highlight. Mouse moves arrive as
   `kCGEventLeftMouseDragged` (already done by `OSXComputer::fakeMouseMove`
   while a button is down).
6. Meanwhile the server starts streaming file data into a staging folder on
   the client (prefetch), so small files are ready by the time the user drops.
7. User releases the button → `mouseUp` forwarded → drop happens. The drop
   target asks the promise for a destination folder; the client moves the
   staged file there (or waits for the rest of the data, reporting progress
   via `NSProgress` so Finder shows a progress bar).
8. Dropped on nothing / Esc → client sends `DragCancel`, staging is deleted.

## Protocol (bump to 1.9)

New proxies `ClientProxy1_9` / `ServerProxy1_9`; only sent when both sides
negotiated ≥ 1.9 and the `dragAndDrop` option is on.

| Code | Format | Direction | Meaning |
|------|--------|-----------|---------|
| `DGBN` | `%4i%s` | both | Drag begin: transfer id + manifest (entries: relative path, size, is-dir, mode, mtime) |
| `DGDT` | `%4i%4i%8i%s` | both | Data chunk: transfer id, entry index, offset, bytes (≤ 64 KB) |
| `DGEN` | `%4i%1i` | both | Entry/transfer finished (with status) |
| `DGDP` | `%4i` | both | Dropped: receiver accepted, sender must finish sending |
| `DGCN` | `%4i%1i` | both | Cancel (by either side, with reason) |
| `DGST` | `%1i` | client → server | Client reports a local file drag started/ended (needed so the server lets the cursor leave a client screen mid-drag) |

Chunks are small (64 KB) and paced so mouse/keyboard messages are never
stuck behind a large file on the shared stream. If that is not smooth
enough for multi-GB files, phase 3 moves bulk data to a second TLS
connection on the same port, authenticated by a one-time token sent in
`DGBN`.

## Code changes

**Shared (`src/lib/deskflow`)**
- `ProtocolTypes`: new codes, `kProtocolMinorVersion = 9`, option id
  `kOptionDragAndDrop`.
- New `DragTransfer` (manifest, serialise/parse, sender that reads files
  in chunks from a worker thread, receiver that writes into
  `~/Library/Caches/Deskflow/Drops/<id>/`).
- `IPlatformComputer`: new virtuals —
  `getDraggedFiles()`, `cancelLocalDrag()`,
  `beginRemoteDrag(manifest)`, `endRemoteDrag()`.

**Server (`src/lib/server`)**
- `Server::isLockedToComputer()` / `onMouseMovePrimary` / `onMouseMoveSecondary`:
  when locked only because of a button, ask "is a file drag active?"
  (primary: platform `getDraggedFiles()`; secondary: last `DGST` from
  that client) and allow the switch if yes.
- `switchScreen`: on a drag crossing, send `DGBN`, start the sender.
- `ClientProxy1_9`: send/receive the new messages.

**Client (`src/lib/client`)**
- `ServerProxy1_9`: handle the new messages, forward to `Client`.
- `Client`: owns the receiver; calls `beginRemoteDrag` on enter.

**macOS platform (`src/lib/platform`)**
- Drag detection: `NSPasteboard(name: .drag)` `changeCount` snapshot on
  mouse-down (extend `OSXPasteboardPeeker`).
- Cancelling the source drag: post an Esc key event (or a mouse-up at a
  no-drop location) before hiding the cursor. Needs a spike.
- Starting the drag on the receiver: the core has no AppKit loop, so the
  drag session lives in a small Cocoa helper — either inside
  `deskflow-gui` (already an `NSApplication`, already linked to the core
  over `CoreIpc`) or a dedicated `DeskflowDragHelper` agent app. It shows a
  1×1 transparent window under the cursor, receives the injected
  mouse-down, calls `beginDraggingSessionWithItems:` with
  `NSFilePromiseProvider`s, then hides the window.
- Synthetic drag events need event numbers on macOS 27 (already handled
  in `OSXComputer.mm`, line ~64) — check the drag session accepts them.

**GUI**
- Server config: "Enable drag and drop of files" checkbox (writes the
  option). Optional: progress / notification on completion.

## Safety

- Off by default; both sides must enable it.
- Receiver only writes inside its staging folder; paths are normalised,
  absolute paths and `..` rejected; symlinks not followed.
- Size cap per transfer (configurable), free-space check before accepting.
- Staging folder cleared on cancel, disconnect, and startup.
- Data rides the existing TLS connection.

## Phases

0. **Spikes (de-risk, ~2–3 days)**: (a) receiver: helper app starts a
   file-promise drag from injected CGEvents and Finder accepts the drop;
   (b) source: detect file drag + cancel it cleanly after crossing.
1. **Server → client, Mac only**: protocol 1.9, server-side unlock,
   streaming, promise drop. Files only, no folders.
2. **Client → server**, folders, progress, cancel paths, option in GUI.
3. Separate data connection for big files; Windows (`IDropTarget` /
   `DoDragDrop`) and Linux/X11 (XDND) backends.

## Spike results (2026-10-26, macOS 27.0)

Code: `tools/filedrag/spikes/` (see `tools/filedrag/README.md`). Both spikes inject events exactly like
`OSXComputer::postMouseEvent` / `fakeMouseButton` (event numbers, click state).

**A. Receiver — file-promise drag from injected events: works.**
- Injected mouse-down reaches a 48×48 borderless helper window;
  `beginDraggingSessionWithItems:` with `NSFilePromiseProvider`s starts a
  native drag; injected `leftMouseDragged` events move it (≈50–60
  `movedTo` callbacks per 2 s drag); injected mouse-up drops it.
- Finder accepts the drop and only then asks for the file
  (`writePromiseTo:`). With a 3 s artificial delay Finder just waits;
  3 files incl. 50 MB arrived intact. Finder requests all promises in
  parallel on our queue.
- Finder resolves name clashes itself (asked for `note 2.txt`).
- Native drag image + count badge render normally.
- Required details: `acceptsFirstMouse` → YES; window not fully transparent
  (alpha 0.02, else clicks fall through); `.popUpMenu` level;
  `NSApplicationActivationPolicyAccessory` (no Dock icon, no activation);
  promise writes on a background `NSOperationQueue`; the helper window can
  be hidden right after the session starts.
- Gotcha seen in testing: in Finder column view the drop goes to the column
  under the cursor (normal Finder behaviour, but worth knowing when
  testing).

**B. Source — detect + cancel: works.**
- Detection: snapshot `NSPasteboard(name: .drag).changeCount` at mouse-down;
  while the button is held, "changed + contains `public.file-url`" means a
  file drag. Detected 23–30 ms after Finder's drag started, and the URLs
  are readable immediately. Cheap enough to check only when the cursor is
  in the jump zone.
- Cancel option 1, **Esc** key: Finder drag cancelled, nothing dropped on the
  landing target, file untouched.
- Cancel option 2, **sink window** (invisible window under the cursor that
  refuses the drag, then a synthetic mouse-up): also cancels cleanly, and
  the sink *sees* the dragged items, so it could also capture
  promise-based drags (Mail attachments, Photos) later.
- Control run without cancel: the drop does land on the target, proving
  the test catches a failed cancel.

**Decisions taken from the spikes**
- v1 cancels with Esc, posted by the core *before* `switchScreen` marks the
  primary as "off-screen" (otherwise Deskflow's own event tap would
  forward the Esc to the client). Only post it when a file drag was
  detected, so a stray Esc never reaches an app.
- The sink-window approach is kept for v2 (promise-based sources), since
  the helper process will exist on both Macs anyway.
- The receiver helper needs an AppKit process: confirmed. The core stays
  AppKit-free and talks to the helper over local IPC.

**Not covered yet** (next step, inside real Deskflow builds)
- Physical button held + Deskflow's event tap active (spikes were fully
  synthetic, on one Mac).
- Receiving side on the client Mac with events coming from the network.
- Destinations other than Finder and a test app (Desktop, Mail compose,
  Slack, browsers).
- Secure input mode and multi-display edge cases.

## Phase 1 as built (server → client, macOS, files only)

Changes from the plan above:
- **Helper process (`deskflow-drag-helper`), after trying without.** A
  first version showed the drag window inside `deskflow-core`. Once a
  process owns windows, HIToolbox asserts that keyboard layout calls
  (`TSMGetInputSourceProperty`, used by `OSXKeyState::pollActiveGroup`
  on every key) run on the main thread, and the core makes them on its
  event thread: the first key press after a drop killed the client. The
  window now lives in a small AppKit helper the core starts and talks to
  over pipes (JSON lines, see `src/apps/deskflow-drag-helper/main.mm`).
- **Primary button state after a handover.** The press happens on the
  primary, the release while the cursor is on the client. (1) The event
  tap now also passes that release to macOS, otherwise the system keeps
  the button "held"; (2) `OSXComputer::isAnyMouseButtonDown` on the
  primary reads the live state instead of the snapshot taken at leave
  (`updateButtons()`), which kept the cursor locked to the client.
- **File reading on a worker thread.** Opening a file can block on a
  macOS privacy prompt (Desktop/Documents folders); the server's event
  thread must not wait for it.
- **Upstream race fixed:** `EventQueue::waitForReady()` missed the ready
  signal if called after the loop started ("event queue is not ready
  within 5 sec", deskflow/deskflow#4276); covered by a new test.
- **End-to-end acks instead of "socket flushed".** `StreamOutputFlushed`
  only means the kernel took the bytes, so the client acks chunks
  (`DGAK`) and the server keeps ≤ 8 × 64 KB unacknowledged.
- No `DGDP`/`DGST` yet: files are always prefetched; client → server is
  phase 2.

Messages (protocol 1.9): `DGBN%4i%s` begin+manifest, `DGDT%4i%4i%s` data,
`DGFE%4i%4i` file end, `DGAK%4i%4i` ack, `DGCN%4i%1i` cancel.

**Folders and progress (added after the first hardware test):**
- Manifest v2 (`'DFM2'` magic): entries with type (file/folder), size and
  a relative path; parents precede children; entries without `/` are the
  dragged items. Data/file-end messages carry the entry index. Paths are
  validated per component; clashing item names get " 2", " 3".
- The server builds the manifest on the worker thread (folder walks can be
  long or blocked by privacy prompts) and sends `DGBN` just after the
  switch; the client starts the drag on arrival unless the button was
  already released since `enter`.
- One drag item per dragged item; folders are folder promises.
- Progress: when Finder asks for an item that is not complete yet, the
  helper creates a placeholder at the destination and publishes an
  `NSProgress` (kind file, receiving, file URL). Finder draws it dimmed
  with a progress bar and a cancel button; the client feeds byte counts
  (≤ 10/s per item). On completion the placeholder is replaced by the
  real item; on cancel/failure it is removed and the server stops sending.
- Finder-like greying (V3): published progress alone draws nothing in
  column view. Finder greys out files whose FinderInfo type/creator is
  `brok`/`MACS`, and then also draws the progress pie for the published
  progress. Folders ignore the marker, so a pending folder is an empty
  marked *file* with the folder icon (no disclosure arrow, like Finder's
  own in-progress folders) until the real folder replaces it.
- Copy window (V3): a non-activating panel in the helper mimicking Finder's
  ("Copie de N éléments dans « X »", bar, "a sur b – Environ n secondes",
  stop button), shown if items are still pending 0.5 s after the drop.
  Sizes/durations are formatted by hand (the helper is not a localized
  app, so Foundation formatters would use English unit words).

Where things are:
- `deskflow/FileDrag.{h,cpp}`: manifest, `FileReader`, `FileWriter`
  (staging in `~/Library/Caches/Deskflow/Drops/<id>/`).
- `server/ClientProxy1_9`: sends with the ack window; `Server`:
  `canHandOverFileDrag()` lets the cursor leave mid-drag, `switchComputer()`
  cancels locally (tagged Esc) then announces before `enter()`.
- `client/ServerProxy1_9` + `Client::fileDrag*`: receive, ack, start the
  drag on enter, cancel back on "no drop".
- `platform/OSXComputer.mm`: drag pasteboard snapshot on mouse-down,
  tagged-event pass-through in the tap, deferred moves until the drag
  window is ready (≤ 1 s).
- Setting `server/enableFileDragDrop` (default on in this fork).

Tests: `FileDragTests` (manifest, names, reader→writer), two new
`ClientProxyTests` (ack window, cancel). End-to-end on one Mac with
`tools/filedrag/fake_server.py` driving a second client: normal drop,
drop before data arrived (Finder waits), and cancel over the menu bar —
all pass. Not yet run: the server half on real hardware.

## Client → server (V4)

- Shared `FileDragSender` / `FileDragReceiver` (deskflow lib); both proxies
  use them, so each side can send and receive. Transfer ids: server from 1,
  client from 0x80000000, so a two-way `DGCN`/`DGAK` is never ambiguous.
- `DGST%1i` (client → server): while the server holds the button for the
  client, the client checks its drag pasteboard (≤ every 150 ms) and
  reports when a file drag starts/stops. `Server::canHandOverFileDrag`
  then lets the cursor leave the client towards the server.
- `DGHO` (server → client), sent before leaving the client: the client
  cancels its drag (tagged Esc), releases the button and sends the files.
  The server accepts the manifest only right after the hand-over and while
  the user still holds the physical button.
- Starting the drag on the server: the user moves the real cursor, so the
  helper's panel follows it; once on screen the core asks it to park at
  the current point (`pressAt`), and only after `placed` injects the
  (tagged) press there. The user's real release passes to macOS and drops.
  The same two-step handshake is used on the client (where moves are held
  back) — it fixed a race where a move between press and delivery could
  pull the panel away.

## Parallel transfers (V5)

- `FileDragSender` runs several transfers: each has its own worker; on the
  wire they take turns chunk by chunk. In-flight limit =
  max(8, 4 × active transfers), so every transfer can reach the receiver's
  ack threshold (4) and none can starve the others.
- `FileDragReceiver` keeps one record per transfer (pending → dragging →
  dropped). A new manifest only replaces a transfer whose drag was not
  dropped yet; dropped ones keep receiving. A transfer is removed (staging
  deleted) once all its items were delivered (`FileDragDelivered`).
- The helper keeps a session per transfer; each promised item carries a
  reference to its session. The copy window shows one row per transfer
  still arriving, each with its own stop button.

## Adaptive window (V6)

Measured between the two Macs (mini on gigabit Ethernet, MacBook on
802.11ax): raw TCP ~70 MB/s; the fixed 8 × 64 KB window capped transfers at
~45 MB/s. Larger windows sped up to 1–1.5 MB, then only added queueing delay
(ping 4 ms idle, 40+ ms with 3–4 MB in flight), which mouse and keyboard
messages wait behind. macOS's background service type on the socket did not
help: the queue builds at the Wi-Fi access point.

- The window is in bytes, starts at 1 MB and stays within 512 KB–16 MB.
  Each chunk counts at least `kMinChunkCost` (16 KB), so small files fill
  the link without queueing thousands of file creations (~0.16 ms each on
  the receiver) ahead of input.
- `WindowControl` (sender only, no protocol change): the lowest ack time
  seen is the base delay; once per round the round's lowest ack time is
  compared with it: < 4 ms extra and data waiting -> +256 KB, > 10 ms extra
  -> ×0.75. Reset when the sender goes idle.
- A finished transfer stays in the in-flight count until its last chunk is
  acknowledged.
- `fully sent` log lines give the duration, MB/s, final window and base delay.

Result on hardware: almost twice as fast, with acceptable mouse lag.

## Large manifests (V7)

A protocol string is limited to 1 MB (`PROTOCOL_MAX_STRING_LENGTH`) and a
message to 4 MB; a manifest takes ~15 bytes plus the path per entry, so a
folder of ~10,000 files already went over and the receiver dropped the
connection (seen with a 67,725-file, 4.7 MB manifest). Manifests over
`kManifestPartSize` (512 KB) now go out as `DGBP%4i%s` parts followed by
`DGBN` with the rest; `ManifestAssembler` joins them on the receiver (up to
`kMaxManifestSize`, 64 MB, else the drag is refused). Smaller manifests are
still sent whole, so older peers keep working for them.

## Folder handover before scanning (V8, protocol 1.11)

A large folder can take several seconds to enumerate. Previously the native
receiving drag only started after the full manifest arrived. If the user
released during that scan, the server refused it with reason `Dropped`, so
the folder never appeared even after fixing the protocol string limit.

- For a drag containing folders, `DGPR%4i%s` announces just the selected
  items before walking their contents. The receiver starts file promises
  immediately; these are not marked ready until the scan and transfer finish.
- The full manifest follows as `DGFM%4i%s`, with `DGBP` parts when needed.
  It populates the same transfer, preserving an already accepted drop.
  Root names, types and order must match the preview.
- Item sizes and entry counts are sent to the helper once known, updating
  Finder progress and the copy window even if the promises were dropped
  while scanning. Cancelling or returning before the drop discards the
  preview; a late full manifest cannot resurrect it.
- These messages and manifest parts are sent only after negotiating 1.11.
  Earlier peers retain `DGBN` for manifests up to 1 MB; larger ones are
  refused locally without disconnecting the peer.
- Receiver tests cover dropping before the manifest, cancellation, changed
  roots and empty folders. `fake_server.py --manifest-delay 3` exercises a
  native drop made before the complete file list arrives.

## Open questions

- Helper inside `deskflow-gui` vs separate agent app (GUI is simpler to
  ship; separate helper works when the core runs headless).
- Eager prefetch vs transfer only on drop (prefetch feels instant for
  normal files but wastes bandwidth on cancelled drags; plan: prefetch up
  to N MB, rest on drop).
