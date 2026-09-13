# Argentum S4 — Kestrel (window manager + global menubar)

Status: **DRAFT (2026-09).** Design + split for S4 of
`docs/design/argentum-milestone-split.md` ("Kestrel — window manager +
global menubar"), on top of the S3 keyboard-complete widget catalog.
The WM's charter is `docs/design/argentum-uikit-plan.md` §5 (Kestrel)
and §6 (menu IPC over the AF_UNIX session socket); this doc turns the
two milestone bullets into observable slices. Same working pattern as
S2/S3: design doc first, one acceptance-gated slice at a time,
committed separately.

## 1. Scope

1. **S4.1 — WM skeleton.** Kestrel becomes a real X11 window manager
   (a from-scratch argentum consumer — the toolkit's first app),
   reparenting client windows into Kestrel-owned frames and drawing
   the window chrome with argentum primitives; it tracks focus and
   publishes it EWMH-style (`_NET_ACTIVE_WINDOW`). Two argentum apps
   run under it, decorated and focus-switchable by click.
2. **S4.2 — global menubar + menu IPC.** Apps publish their toolkit
   `Menu` model over the AF_UNIX session socket (config-framed wire —
   the seam the `menu.cpp` header already reserves: "the config-framed
   wire format + the session socket (Kestrel) come later"); Kestrel's
   top menubar renders the focused app's menus and routes picks back
   to the owning app's `MenuItem` actions.

The uikit-plan §5 desktop chrome (wallpaper surface, right dock,
date/time) and the session/workspace behaviour belong to S5 and are
**out** of S4 (recorded under §6).

## 2. New machinery

- **Kestrel = a reparenting WM process.** It selects
  `SubstructureRedirect` on the root; every `MapRequest` for a managed
  window creates a Kestrel-owned frame X window, reparents the client
  into it, sizes the frame = title band + client, and maps. The client
  keeps drawing into its own window unchanged (argentum apps need no
  WM-specific code to be *managed*). ConfigureRequests are obeyed
  (the client may not resize itself in v1 — it maps once; the frame
  is placed in the work area: below the menubar, with a small margin).
  Kestrel's own surface = one argentum window (the menubar strip)
  which Kestrel keeps raised above the frames.
- **Frame chrome via argentum.** The frame's title band is painted by
  Kestrel with `GraphicsContext` over a per-frame `BitmapImage` +
  an XPutImage flush (the same pixman composite path as
  `Window::flushBacking`). v1 chrome = a solid page/chrome band with
  the client's `WM_NAME` text and a close glyph at the right, plus a
  border ring; the ACTIVE frame's title band is accent-tinted, the
  inactive frames keep the neutral chrome. (A toolkit-level frame
  *view* is deferred — Kestrel paints frames directly.)
- **EWMH focus.** Kestrel tracks focus by `EnterNotify`/`ButtonPress`
  landing in a managed client's window: it raises that frame, paints
  the active title, and writes `_NET_ACTIVE_WINDOW` (root property,
  `ClientMessage` per EWMH) so focus is externally observable.
  Click-to-focus; click-to-raise is implied (raise on focus).
- **Session socket (§6).** Kestrel listens on an AF_UNIX socket under
  the FSH temporary-files convention (the X11 sockets' home,
  `/System/Temporary Files/argentum-session`, `kSessionSocketPath`).
  Apps connect at map time and publish their menu model: menus/titles,
  item kinds (action/check/radio/separator), enabled state, keyboard
  equivalents, item ids. Picks flow back as triggers (id -> app); model
  diffs (enable/disable/relabel) can follow the same records. The
  toolkit has the serialize/parse pair over its `Menu`/`MenuItem` model
  plus the client-side connect/publish helper; Kestrel deserializes
  into the same model for its bar.
  **As built (S4.2a) — the wire is NOT config-framed, and that is a
  decision, not an oversight:** the plan said "config-framed records",
  which assumed libconfig could render a tree to memory and parse one
  back. It cannot: its tree has no public construction API, its only
  write path is load-modify-write of a whole domain FILE (temp fd +
  rename), it exposes no render-to-string or parse-from-buffer, and its
  string renderer does not handle RECORD nodes at all. What is built
  instead is a small self-contained line-record codec in `menu.cpp`
  (documented at its head: a version header, one tab-indented record
  per node with the title last, titles escaped, a 64KB frame cap, a
  depth cap, strict rejection of anything malformed) framed as
  `PUBLISH 0x<xid>` + that record, with a 4-byte big-endian length on
  the socket. Two reasons this is the *right* shape and not a
  compromise: a session socket is IPC, not configuration (libconfig
  stays the config system's), and the payload is line-oriented so the
  WM can log exactly what it parsed — which is how the S4.2a gate
  checks it. Extending libconfig with public memory render/parse is
  worth doing on its own merits some day; it is not on this path.
- **WM_DELETE + the toolkit.** Closing: Kestrel's close glyph sends
  `WM_DELETE_WINDOW` (ClientMessage). `argentum::Window` gains
  `WM_DELETE` handling: a `setOnClose(std::function<void()>)` hook
  (default: quit the app) so managed apps can exit cleanly.

## 3. API surface

```cpp
/* toolkit: menu wire (menu.cpp — the seam its header reserves) */
bool menuSerialize(const Menu *root, /* appends config-framed */ ...);
Menu *menuParse(...);			/* item ids preserved */

/* toolkit: session publisher (client side of §6) */
class SessionMenu {
  bool connect();			/* AF_UNIX to Kestrel's socket */
  bool publish(const Menu *menubar);	/* send the model */
  bool sendPick(int itemId);		/* ... */
};

/* argentum::Window: WM close */
void setOnClose(std::function<void()> cb);	/* WM_DELETE */
```

Kestrel itself is a new app (`userland/kestrel/`) with its own small
WM core (root selection, frame map, focus/EWMH) + an argentum window
for the menubar strip; frame painting reuses `GraphicsContext`.

## 4. Split (one observable acceptance per slice)

### S4.1a — WM core: reparenting + decorated frames
*Acceptance:* Kestrel runs; two argentum probe apps map under it. Each
app's content appears inside a Kestrel frame with the argentum chrome
title band showing the app's `WM_NAME`; the frames sit in the work
area below the menubar strip. Screendump + Kestrel logs (`KESTREL:
manage <xid> '<title>'`).
Status: **DONE** — `userland/kestrel/kestrel.cpp`: a reparenting WM on
the toolkit's own connection (`Application::display()` +
`Application::setEventHook`): SubstructureRedirect on the root (with a
BadAccess guard), every MapRequest gets an argentum::Window frame whose
`FrameChrome` content view draws the chrome title band (title +
close-glyph plate), the client is reparented below the band at its
requested spot clamped into the work area below the strip, and a
full-width `StripView` strip (the future menubar) sits at the top.
Init gains `desktop = "kestrel"` (SESSION_KESTREL); `make kestrel-img`
builds `.build/rootagfs-kestrel.img`. Probes `krel_a`/`krel_b`, gate
`.build/s41a_run.sh` + `s41a_assert.py` -> S41A-OK (10 checks:
two manage logs with titles/positions, strip + both frame bands in
chrome, each probe's content at its band-offset position, title text
in the band). Load-bearing: the WM must select redirect on the SAME
connection the event loop reads (the hook only sees that display), and
the strip maps via XMapWindow — the toolkit's show() focus grab races
the map (BadMatch, and Xfb faults on the error path).

### S4.1b — EWMH focus + active chrome
*Acceptance:* clicking inside probe B raises B and paints B's title
band active (accent); the root's `_NET_ACTIVE_WINDOW` = B's xid
(Kestrel logs it); clicking A swaps both back. Screendump shows one
accent band vs one neutral band.
Status: **DONE** — Kestrel tracks the active client: the first managed
window takes focus; a click inside a client (or on its frame) focuses
it — the WM sees client clicks through a passive `XGrabButton` +
`XAllowEvents(ReplayPointer)` (ButtonPressMask is an AtMostOneClient
event: the client already selected it, so a plain XSelectInput would
BadAccess — the grab + replay is the standard WM mechanism). focusClient
raises the frame, repaints both bands via `FrameChrome::setActive`
(armed/accent band + light label vs idle chrome), sets the input
focus, publishes `_NET_ACTIVE_WINDOW` and logs `KESTREL: focus`.
Kestrel keeps a non-fatal X error handler. Gate `.build/s41b_run.sh`
+ `s41b_assert.py` -> S41B-OK (7 checks: A then B focused, b0 shows
A accent/B neutral, a real monitor click on B's content flips the
bands, contents stable). S41A re-run green with its updated
expectation (the first-managed window now carries the active accent).

### S4.1c — WM_DELETE close
*Acceptance:* clicking the active frame's close glyph sends
`WM_DELETE_WINDOW`; the probe exits via `setOnClose` (logs
`S41C-CLOSE`) and Kestrel unmaps the frame (`KESTREL: unmanage`).
Status: **DONE** — the toolkit advertises `WM_DELETE_WINDOW`
(`XSetWMProtocols` in Window::init) and the run loop's new
ClientMessage case calls `Window::handleCloseRequest` (the
`setOnClose` hook, default = quit the app). Kestrel's `FrameChrome`
close plate fires a callback on a band click that sends the
WM_DELETE ClientMessage to the client. Because this server never
delivers DestroyNotify for a client killed by its connection closing,
Kestrel reaps dead clients by probing them: `Application::setIdleHook`
makes run() poll with a 250ms beat when no events pend, and the hook
reaps frames whose client no longer exists. Gate `.build/s41c_run.sh`
+ `s41c_assert.py` -> S41C-OK (7 checks); S41A/S41B re-runs green.
Load-bearing fixes found here: a WM XSelectInput on one of its OWN
toolkit windows REPLACES the toolkit's mask on the same connection —
always OR into `fa.your_event_mask` (the frames had stopped selecting
Exposure and never drew); map the frame BEFORE the client so its band
gets its own Expose.

### S4.1d — title-band drag (move)
*Acceptance (user: "windows aren't draggable yet"):* a real mouse
presses a managed frame's title band (off the close plate) and drags;
the frame follows and is dropped at the pointer. Moved windows render
at the new position; the old spot empties; other windows are
untouched.
Status: **DONE** — Kestrel's event hook starts an active pointer grab
on a band press (the close plate is left to the toolkit so the
FrameChrome can still close) and logs `KESTREL: move ... to x,y`.
v2 (perf fix — per-motion XMoveWindow made the server discard the
window's pixels, so the client fully re-rendered on every Expose):
the drag moves a cheap XOR **outline** on the root and the window is
teleported ONCE (one move + one redraw).
v3 (user: "the window advances a couple px, drops, then repeats along
the motion"): QEMU's mouse path delivers phantom press/release pairs
mid-drag (ps2 sync slips under fast motion), so ending on a release
teleported every couple of pixels. The drag now ends only when the
pointer goes **quiet** — the ~250ms idle beat drops the window at the
outline's last position; releases are consumed but never trusted, and
a phantom re-press mid-drag re-anchors + re-grabs without disturbing
the outline. Gate `.build/s41d_run.sh` + `s41d_assert.py` -> S41D-OK
(a multi-step drag lands at 210,190; a mid-drag screendump proves the
window has NOT moved while the inverted outline tracks over the
desktop; the old spot empties, the band + content render at the new
origin, Krel B untouched). S41A/B/C re-runs green after v3.
v4 (quiet-end had two blind spots): a drag dropped when the pointer
went QUIET even while the button was still HELD (pause mid-drag), and
conversely a released-but-still-gliding pointer deferred the drop
until the motion stopped. The drop is now gated on the button alone:
`dropIfReleased` (per-event + idle beat) drops once the button has
stayed UP for `DRAG_DROP_MS` (250ms). Phantom release/press pairs are
millisecond up-flips — the phantom press re-arms `gBtnDown` long
before the window elapses, so only a real release (button left up)
drops the window, at the outline's last position. S41D gate re-run
green after v4.
v5 (user: "the fully-drawn window keeps moving while the mouse
button is held down"): v4 gated the drop purely on the button, so a
phantom RELEASE that was not re-armed by a press within 250ms dropped
the drag MID-MOTION — the window teleported to the outline and a
later phantom press re-anchored it, so under fast real-mouse motion
the window chased the outline in steps. The drop now requires the
button up AND the pointer QUIET: every MotionNotify re-arms the
button-up clock (`gBtnUpMs`), so motion flowing after a phantom
release can never drop the drag — only a release that stays up and
then rests drops (250ms after the last motion). Mid-drag pauses still
never drop (button held). Repro gate `.build/s41e_run.sh`: a phantom
release followed by >250ms of continuous motion must leave the window
unmoved (S41E-PASS: mid shot == pre shot; the drop fires once, at
rest). S41D re-run green after v5.
v5.1 (user: "the XOR-frame feels laggy, it can't keep up with the
pointer"): dead-client reaping ran on EVERY event (kestrelHook), and
each reap is a synchronous `XGetWindowAttributes` round-trip per
managed client — during a drag every motion was throttled to server
round-trip latency. Measured saturation (250 injected moves): **66
ev/s** sustained with reaping per event vs **135 ev/s** with reaping
moved to the idle beat only (a ~250ms poll that fires when no events
pend, plenty for frame cleanup — dead frames linger at most one beat
after the pointer rests). A paced ~100 ev/s fast-drag burst then
tracks cleanly (10.4ms mean inter-event). S41C/S41D/S41E re-run
green after v5.1.
v6 (user: "have a peek at how some other MIT-licensed WM handles
drags"; direction decided: fix the move cost, then go live): the
outline model is RETIRED. Live per-motion moves were believed to make
the server discard the client's pixels (v2's premise); measured on the
current Xfb that is false — `miMoveWindow`/`fbCopyWindow` carry the
frame AND the reparented client's pixels (a 200-move drag caused
exactly ONE client redraw, and a live drag sustains **285 ev/s** vs the
outline's 135). Kestrel now moves the frame live per MotionNotify like
dwm/cwm/i3; the XOR outline GC, the teleport, and the outline gates
are gone. The END is only a grab release (the window is already where
the pointer is, so ending early/late is visually harmless) and keeps
the button-up + quiet rule with motion re-arm plus the abrupt-release
distrust window (v5/v5.1) — a phantom release stops nothing (motion
keeps the drag alive through ps2 desyncs; the window follows live), a
mid-drag input stall is survived via DRAG_DROP_FAST_MS, and a real
release ends the drag at rest. Client death mid-drag now ends the drag
before the frame is freed (gDragFrame UAF guard in unmanageClient +
reapDeadClients). Gates reworked for live semantics: s41d asserts the
window FOLLOWS mid-drag (band+content at the tracked origin, old spot
empty); s41e (phantom release + continuous motion -> window keeps
following, one in-place end); s41g (abrupt release + ~350ms stall ->
drag survives and follows the full distance; a fixed-250ms window dies
at the stall). S41C/S41D/S41E/S41G all green after v6.
v6.1 (user: dragging a window partially off-screen "wipes the part
that leaves the screen area, and redrawing it is very very slow"):
with live moves a partial-off-screen excursion loses the off-screen
pixels screen-side (no retention), and dragging back re-exposes the
re-entering strips — argentum's Expose handler re-composited the whole
view tree into its backing on EVERY server Expose, so a heavy window
(the zoo, 960x720 of controls) re-rendered per drag step. Server-
generated Exposes (send_event=false) now take `Window::redrawExposed()`:
the client backing store already holds the content, so the damaged
rect is flushed straight from the backing — no view re-render. The
view tree is still re-composited for SELF-sent Exposes (send_event, a
content change). `painted` tracks backing validity (cleared on resize,
set after the first full draw) so pre-first-paint and post-resize
server Exposes fall back to the render path. Verified by the s41h gate:
a full off-screen drag-out + drag-back of krel_a produced exactly ONE
client redraw (the initial paint) with the content intact throughout;
s41d/s41g re-runs + the zoo-under-Kestrel smoke (zook) green after
v6.1.
Note: with live moves there is no outline to hide; the window itself
renders at every tracked position, so drags look native (the earlier
root-outline-hidden note is moot).

v7 (user: "when I drag a window sometimes it 'sticks' dragging even
after I let go the mouse button"): the deferred end WAS the bug.  The
button-up + quiet rule (v5/v5.1/v6's `DRAG_DROP_MS`/`DRAG_DROP_FAST_MS`
and the abrupt-release distrust window) existed to survive ps2 sync slips
and the xHCI event-ring stall, and both are gone: the shipped session is
USB HID with i8042 off, and the stall was fixed in f42fab1.  What the
rule did instead: a release within 60ms of a motion kept the drag alive
for a *full second*, and every motion while the button was up BOTH moved
the window AND re-armed that clock — so a flick followed by any hand
movement dragged the window for as long as the hand kept moving.  The end
is the release itself now; a release lost in the input path is caught
from the stream rather than from a timer, because a MotionNotify carries
the button state at its own time, so motion with the drag button up ends
the drag instead of following the pointer.  `gBtnDown`/`gBtnUpMs`/
`gLastMotionMs`/`gAbruptRelease`/`dropIfReleased` and the DRAG_DROP_*
windows are gone.  On the input side, `mousedev_event` no longer lets a
full queue drop a record carrying a button or wheel transition
(`MOUSE_EDGE_RESERVE`): the old policy said "the pointer merely misses one
motion sample", but a dropped *release* is exactly a stuck drag.  Measured
both ways by the new `wm_dock/drag-ends-at-release` — a flick drag of
(150,-30) followed by a 200px post-release pointer move ends the frame at
170,70 with the fix and at **370,70** without it (the window followed the
released pointer).  wm_dock 12/12 after v7.

### S4.2a — session socket: publish + menubar render
*Status: **DONE** (2026-09).* `userland/argentum/menu.cpp` (the codec +
`SessionMenu` + `sessionWriteFrame`), `application.cpp` (the loop's fd
seam, `setMenuBar`/`setOnMenuPick`, publish on `MapNotify`),
`window.cpp` (override-redirect), `userland/kestrel/kestrel.cpp` (the
socket server, the per-window model, the strip's titles).
*Acceptance:* probe A (and B) publish distinct menus (File/Edit …)
over the session socket; Kestrel's menubar strip renders the focused
app's menu titles in the bar chrome (screendump + a Kestrel log of the
parsed tree).

**v8 (2026-09) — the strip's own repaints never reached the screen.**
Reported as "after I kill and restart the widget zoo the bar shows no
menus, though they are there and react to clicks": the *model* and the
*hit-test* were right, the pixels were not.  Measured, not theorised -

- Kestrel paints the titles (`strip-draw … n=3`) and flushes the whole
  strip rect every time (`flush … rect=0,0 1920x30 shm=1`);
- the window is `map=2` (viewable), correctly placed, and **above** the
  desktop in the stacking order (`XQueryTree`: desktop → dock → strip →
  frame), so it is not obscured;
- yet the strip's screen content is byte-identical before the launch,
  after the kill and after the relaunch (`diff_box == 0`): frozen at the
  boot paint ("Kestrel" + the mark), which is why the menus were
  clickable but invisible;
- A/B on the transport: the same puts through `XPutImage` **do** land
  (the bar's text then reaches x=210 with the zoo active vs 85 at boot),
  while `XShmPutImage` never does for this window.  `window.cpp` now
  routes windows shorter than `ARGENTUM_SHM_MIN_HEIGHT` (32px) through
  the fallback, with the measurement in the comment;
- the dock (same toolkit, same SHM path, 1050px tall) updates fine, so
  the *server-side* difference is still unexplained: **open item** - the
  next measurement is to log inside Xfb's `ShmPutImage`/`PutImage`
  whether the screen damage report fires for the strip's window
  (`hw/xfb/InitOutput.c`'s shadow drain is what copies shadow → fb0).

Guarded by `wm_dock/app-menus-in-bar` (rightmost ink in the bar's rows,
left of the clock zone, before vs after the app becomes active, so a
clock tick cannot move it): it passes on the fixed build and fails on
the frozen one.

**v9 (2026-09) — S4.2d: the app draws its own menus, the WM places
them.**  The publish/PICK protocol is gone.  Each app draws its own menus
in its own borderless, menubar-sized window, which the WM places over its
half of the bar and maps only while that app is the focused client — so
only the active app's menus are ever on screen, and no pick crosses a
socket.

The contract (no IPC):

- the app creates a **normal** window (not override-redirect: the WM must
  see its MapRequest), maps it **without** grabbing focus
  (`Window::show(bool focus)`; the toolkit's `setMenuBar` uses
  `show(false)`), and marks it before mapping: `_ARGENTUM_MENUBAR` = 1 and
  `_ARGENTUM_MENUBAR_FOR` = the app's first ordinary window;
- the WM never frames a marked window: `manageBarWindow()` — hooked at the
  **top** of `manageClient`, so the boot pre-manage sweep is covered too —
  records the binding and lets `barsRefresh()` decide visibility.  The
  zone is the arithmetic the strip's own layout used (after the system
  mark and the focused app's name, before the clock's reserved zone),
  published as `_ARGENTUM_MENUBAR_ZONE` and logged as
  `KESTREL: menubar zone x=226 w=1510`;
- `barsRefresh()` runs from `stripRefresh()`, so focus, the app's title
  and the clock's width all re-assert the placement; it maps the bar only
  while its owner is `gActive`, hides the rest, and re-raises it after
  every strip raise;
- the presses are the app's own: the toolkit hit-tests with the layout it
  painted (the titles are measured with the same rule the strip used) and
  opens its own popup (`popup.cpp`), so the dropdown and the picked item's
  action both run in the app's process.  `menuBarRefresh()` just redraws.

Deleted with the protocol: `menuSerialize`/`menuParse`/`SessionMenu`/
`sessionWriteFrame`/`kSessionSocketPath` and the codec's unit test
(`userland/tests/menu_wire.cpp`), and the WM's `sessionOpen`/
`sessionPublish`/`sessionRead`/`sessionAccept`/`sessionMessage`/
`sessionDropConn`/`sessionSendPick`/`menuForClient`/`logMenu`/
`dropMenusForClient`/`popUpClientMenu`/`clientNameFor`/`stripMenuLayout`
plus the `SessionConn`/`PublishedMenu`/conn/menu tables.  `arrangeWindows`,
`systemMenu` and `popUpSystemMenu` lived inside that block and stayed.

**Two defects the switch exposed, both measured:**

1. *The dropdown opened over the bar.*  Reported as "pulldown menus should
   appear with their tops aligned to the bottom of the menubar, but they
   appear on top of it instead".  The app's bar window sits ON the bar, so
   its origin is the screen's top edge — anchoring the menu at that origin
   covered the very titles it belongs to.  The anchor is now the window's
   own height, the convention Kestrel's system menu already used
   (`menuPopUp(m, 0, BAR_H)`).
2. *The app's bar painted and was then hidden.*  The titles never reached
   the bar while everything reported success, because the WM raises its
   strip to the top at four sites (startup, `arrangeWindows`,
   `manageClient`'s frame raise, the dock raise) and only `barsRefresh()`
   ever raised the bar — so a raise *after* the last placement covered it.
   Found at the writer: instrumenting the app's own draw made the guest log
   show `ARGENTUM: menubar draw 1510x30` (the app *did* paint, at the right
   size) while the bar's rows showed no ink right of the zone — painted,
   not visible.  Every strip raise now re-raises the focused app's bar.

**v8's guard claim is corrected.**  `wm_dock/app-menus-in-bar` measured
x=210, and 210 is *Kestrel's own* text: it draws the app's name in its own
half of the bar (28 + the name's width), so the check passed while the
app's titles were invisible.  It now waits for the app's bar to paint and
measures ink **right of the logged zone** — which is what "the app's menus
are in the bar" means — and it fails on the build where the bar is covered.
The same case's close/relaunch leg closes the app through its close box
(`WM_DELETE_WINDOW`, the same `DestroyNotify` the WM sees): toybox's
`killall` cannot do it, because it reads `/proc` and this boot's mounts do
not expose one (`killall: no /proc`).

Still open, unchanged: why a ~30px window's `XShmPutImage` never reaches
fb0 (v8).  `ARGENTUM_SHM_MIN_HEIGHT` is load-bearing for the app's bar
window too — it is exactly that height.

**The publish/PICK design itself is superseded** - see the redesign plan
in the memory record `kestrel-menubar-app-drawn-plan` (an app-drawn bar
window replaces the protocol; this strip is why it is also the natural
fix for the class above).

**As built (deviations and what the slice uncovered):**

- **The model had to grow first.** `MenuItem` had no kind, no id and no
  key equivalent, and `Menu` had no separators, so the wire had nothing
  to carry: `MenuItem::Kind` (Action/Check/Radio/Separator), a pick id
  (`Menu::addItem` assigns a process-unique one to any pickable item
  without one, so a published menubar always has ids to pick with), a
  key equivalent (character + `KeyMod*` bits), `Menu::addSeparator()`
  (the one item kind a Menu owns) and `Menu::itemWithId()` (the app's
  pick handler, depth-first).
- **`publish(menubar, window)` takes the window**, not the planned
  `publish(menubar)`: the socket is a separate connection from the X
  connection, so the message has to say which window the bar belongs to
  (and Kestrel keys its model by that xid). V1 therefore also has no
  client-side `sendPick` — the pick direction that exists is WM -> app,
  which arrives through the loop's fd hook and dispatches as
  `Application::setOnMenuPick` + the item's own action.
- **The toolkit's loop gained an fd seam** (`addFdHandler` /
  `removeFdHandler`): while one is registered the loop polls the X
  connection *and* those fds and never blocks in `XNextEvent`. Without
  a registered fd the old path runs unchanged.
- **Two toolkit bugs found in use, both fixed here:**
  - `PopupWindow` mapped as an ordinary window, so a WM framed a
    transient menu — it is override-redirect now
    (`Window::setOverrideRedirect`), which is also what keeps a popup's
    xid out of the menubar protocol.
  - `Window::show()` called `XSetInputFocus` before the server had made
    the window viewable, which under a WM (a redirected map) trips
    `BadMatch` on *every* launch; focus is now set only once the window
    is viewable (the WM focuses it when it manages it instead).
- **The strip marks its own damage** (`View::setNeedsDisplay`) before
  drawing: `Window::draw()` composites and flushes only the *pending*
  damage rect, so a content change that does not report itself would be
  painted into the backing and never reach the screen.
- **Deferred, recorded (trust):** the publish names its window, and the
  AF_UNIX layer carries no peer credentials (`SO_PEERCRED` /
  `SCM_CREDENTIALS` did not exist here), so *any* session client could
  claim another app's window and give it a different menubar. The
  window's own bar cannot be forged *against the WM* (the WM draws it),
  but its content can be. Closing this needs peer identity in the
  transport, and belongs with the sessionmgr work.
- *Gate:* `.build/s42a_run.sh` + `s42a_drive.py` + `s42a_assert.py`
  (S42A-OK, 11 checks; the pixel evidence is read as *word runs*, not a
  fixed x window, so it does not depend on where the theme's font size
  puts a word — the first version of this gate misread "File Edit" as
  "no menus" by sampling past both words).

### S4.2b — picks + focus swap (whole S4)
*Status: **DONE** (2026-09).* `userland/argentum/argentum.h`
(`menuPopUp`/`menuPopUpDismiss`), `popup.cpp` (the pick handler + the
one-at-a-time bar dropdown), `userland/kestrel/kestrel.cpp`
(`stripMenuLayout`, the strip press →`menuPopUp`, the PICK send).
*Acceptance:* clicking a bar title drops its items; picking an item
routes the trigger back to the owning app and its `MenuItem` action
runs (the app logs it). Switching focus between A and B re-renders
the bar with the newly focused app's menus. Original S4 acceptance:
two apps, the bar swaps with focus, picks trigger app actions.

**As built:**

- **The toolkit presents the menu, the WM routes the pick.** A new
  public `menuPopUp(menu, xRootPx, yRootPx, onPick)` rides the existing
  `PopupWindow`/`PopupMenuView` (S2.3c). With `onPick` a row click hands
  back the item's **id** instead of running the item's own action — which
  is the whole point: Kestrel holds the *parsed* copy of the focused
  app's model, so it cannot run the app's item, and must not pretend to.
  One bar dropdown at a time, owned by `popup.cpp` and reused for the
  next menu (a different menu needs a differently sized window, so the
  old one is deleted there — never inside its own callback).
- **The strip's layout is one function**, `stripMenuLayout`, used by both
  the paint and the hit-test (`bandControlAt`'s rule), so a title cannot
  be painted somewhere other than where it is clickable. It skips
  separators and reports each title's extent and menu index.
- **Press rules:** a press on the strip on a title drops that title's
  menu; a press on the strip elsewhere, on a frame, or in a client
  closes an open one. Presses *inside* the popup are left to the toolkit
  (its own blank-click dismissal). All of it consumed by the WM's hook,
  which is also what keeps the toolkit's `_popupDismissOther` from
  killing the dropdown the WM just opened.
- **The pick travels home** as `PICK 0x<xid> <id>` over the owning
  app's session connection (the app that published that window's
  menubar), and the app's `SessionMenu` dispatches it to
  `Application::setOnMenuPick` plus the item's own action via
  `Menu::itemWithId`. Ids are preserved end to end by the wire codec,
  which is why the id Kestrel sends is the id the app acts on.
- *Gate:* `.build/s42b_run.sh` + `s42b_drive.py` + `s42b_assert.py`
  (S42B-OK, 17 checks). The driver reads *where* to click from the
  screenshots (the bar's word runs, then the dropdown's first row), so a
  font-size change cannot stale it; the popup is located by its light
  chrome fill, because the desktop wallpaper and the frames are dark too
  (the first version measured the wallpaper). Assertions include that
  each pick ran in *its own* app (`KREL-A-ACTION New` and not
  `KREL-B-ACTION New`) and that the dropdown is on screen while open and
  gone after the pick.

### S4.2c — the reference app's menubar, and real menu rows
*Status: **DONE** (2026-09).* `userland/tests/widget_zoo.cpp` (the zoo's
menubar + `Application::menuBarRefresh`), `userland/argentum/popup.cpp`
(per-row heights, separators, marks, key equivalents),
`userland/argentum/menu.cpp` (the checked bit), `view.cpp` (a hide/show
repaint fix it uncovered).

*Acceptance:* the zoo — the app a person runs to see the toolkit — has a
real menubar whose items do things the board can be seen doing, and the
dropdown draws what the wire carries.

**As built:**

- **The zoo's menubar is real**: `zoo` (About, Quit), `Widgets` (Reset
  Values, then Enable All / Disable All), `View` (a Check item over the
  TabView, Focus First Field, Dump Geometry), with `⌘Q`/`⌘R`/`⌘D` key
  equivalents. Each action drives board state and logs it, so a gate can
  see it happen, and the reset reuses the action the board's own popup
  menu runs.
- **`MenuItem` gained a checked state** and the wire carries it: a Check
  or Radio item's mark is meaningless without it. It rides the record's
  `flags` bit 1 (still version 1 — the flags field was already numeric),
  so `menuSerialize`/`menuParse` and the round-trip probe cover it.
- **`Application::menuBarRefresh()`** republishes the model for the
  app's windows. A Check item's state changes in the app; the WM holds
  a *copy*, so without this the tick could never change. (A *diff*
  protocol is still not needed; §S4.2a's "model diffs can follow the
  same records" now has its first real user.)
- **The dropdown now draws a real menu**: rows are not all the same
  height (a separator is a 9pt rule inset from the edges, not a text row
  of blank), a checked Check item shows `✓` and a checked Radio item a
  dot in a mark column (titles start past it), and a key equivalent is
  drawn right-aligned. `⌘`/`⇧` are glyphs; `Ctrl+`/`Alt+` spell
  themselves out rather than risk a missing glyph in the UI font. The
  popup's width now fits the mark column and the equivalents.
- **`View::setHidden` did not repaint** — found in use, and the reason
  this slice exists. It flipped `hidden` and *then* called
  `setNeedsDisplay()`, which returns early for a hidden view, so the
  area a view vacated was never redrawn: with the damage-limited flush
  the pixels stayed on screen forever. Both directions are now reported
  before/after the flip. (Anything that hides a view had stale pixels —
  scrollbars, panels, and this.)
- *Gate:* `.build/zoomenu_run.sh` + `zoomenu_drive.py` +
  `zoomenu_assert.py` (ZOOMENU-OK, 13 checks) — a fresh boot with the
  zoo as the only client, so its bar is the only bar. It clicks "View"
  and picks the Check item, then asserts: the zoo's action ran and said
  so, the pick was routed to its window, the model was published again,
  the TabView left the screen (18k pixels), a separator is drawn as a
  rule (150px), and the mark column has ink while checked (19px) and
  none after the toggle (0px). The driver reads where to click from the
  screenshots, and the WM draws the app's *name* before the menus, so
  the zoo's "View" is the last word run, not the second.

## 5. Files

- `userland/kestrel/` — the WM (new app; root handling, frame
  manager, focus/EWMH, the argentum menubar window, session-socket
  server).
- `userland/argentum/menu.cpp` — wire serialize/parse + the
  `SessionMenu` client helper; `window.cpp` gains `setOnClose`.
- Probe apps `userland/tests/krel_a.cpp` / `krel_b.cpp` (two managed
  apps with menus + a close hook); gates `.build/s41{a,b,c}_run.sh` +
  `.build/s42{a,b}_run.sh` with the usual screendump + pixel/log
  asserts.
- Markup: `docs/design/argentum-milestone-split.md` S4 bullets and
  `argentum-uikit-plan.md` §5/§6 as the slices land.

## 6. Deferred (decisions, not omissions)

- **Move and client resize are both DONE** (S4.1d title-band drag;
  S4.3 edge/corner grips — this bullet used to defer the resize to
  S5). What remains deferred in the chrome: **minimize** still needs
  the task list and **shading** (double-click the title bar) is
  undesigned.
- **Desktop chrome** (wallpaper surface, right dock + trash,
  menubar date/time): uikit-plan §5 items = S5's session surface.
- **Session/login** (sessionmgr, greeters, power menus): per
  `sessionmgr-design.md`; the bar's System menus come with it.
- **Menu model extras**: separators/check/radio kinds ride the wire
  format from day one but v1 acceptance exercises action items;
  keyboard equivalents in the wire are spec'd and unused until
  mnemonics land.
- **A11y over the session socket**: rides the same seam later (§6 of
  the uikit plan); the socket stays framed so a codec can join.

## S4.3 — Platinum window frames, resize affordance in the chrome

*Status: **DONE** (2026-09).* Built in `userland/kestrel/kestrel.cpp`
(the frame, the resize mode of the drag session, the zoom + toolbar
boxes) and `userland/argentum/` (the two published hints in
`Window::setPreferredContentSize` / `setToolbarHeight`); probes
`userland/tests/krel_a.cpp` / `krel_b.cpp`; gate `.build/s43_run.sh` +
`s43_assert.py` -> **S43-OK** (36 checks: the outline, the 20px band,
the three band controls and the grow box in the right theme tones, an
edge drag that resizes the frame AND the client, the toolbar box's round
trip — telling the client to hide/show its strip AND reserving the
geometry each way — the zoom to the published size, the S4.1d move still
live, the close box, and the created-but-undrawn moment).
This supersedes the first version of this section, which put the handle
in the client's content view. That was the wrong side of the line: the
frame is the WM's, so the affordances on it are the WM's too.

**Why the move is right.** With the handle in the client, the client had
to resize itself — wrong under a reparenting WM, and the reason that
draft needed a `_NET_SUPPORTING_WM_CHECK` probe plus an
`_NET_WM_MOVERESIZE` round-trip to do what the WM can do directly. With
the handle in the frame, nothing but the WM ever resizes anything: no
protocol round-trip, no WM detection, no client-side gesture state, and
no way for an application to leave the frame behind. Platinum was
already the decided look (`docs/design/system-extensibility.md`:
"Platinum is built in, not bolted on"); Kestrel's frames are the current
deviation — a 26px band, accent-tinted when active (S4.1b).

**The frame** (`FrameChrome`):

- a 1px outline around the whole frame, in the theme's outline tone (see
  Coloration), carried by X's window border (`XSetWindowBorder`) rather
  than painted, because the sides and bottom of the frame window are
  occupied by the client;
- a ~20px title bar, keeping S4.1b's accent tint as the active cue -
  decided: the tint stays and the title bar is NOT pinstriped. Platinum
  supplies this frame's shapes (outline, boxes, grow box), not its
  title-bar texture;
- the title band's controls, in Mac OS X order (decided): **close** and
  **maximize** at the left, the title **centred**, and a **show/hide
  toolbar** button at the right. Minimize belongs between close and
  maximize and is deferred, not dropped (below). This supersedes the
  Platinum close-box/zoom-box arrangement: the frame takes its *shapes*
  from Platinum and its *controls* from OS X, which is the
  Snow-Leopard-parallel line the catalog was cut along;
- **maximize means grow to fit the content**, not fill the screen. The
  client publishes the size it wants - the toolkit knows it, it is its
  content view's `contentSize()` - and the WM's button resizes the frame
  to that. One new piece of protocol: an atom the toolkit sets and
  Kestrel reads, since nothing in WM_NORMAL_HINTS carries "the size this
  content wants";
- **minimize is deferred until there is a task list** (decided). Kestrel
  has neither a Dock nor a task list, so iconifying today would make a
  window vanish with no way back. The button appears with the task list;
- **show/hide toolbar**: the frame RESERVES a strip under the title band
  and the CLIENT draws into it (decided). So the client's window includes
  the strip and the toggle is about geometry - the WM adds or removes the
  strip from the client's rect - rather than the client redrawing chrome
  it was never told about. The strip's *height* is the application's to
  choose, so the client publishes it alongside its preferred size.
  Amended in the build (user decision, 2026-09): geometry alone cannot
  express the toggle - a client cannot tell "the WM hid my strip" from
  "the window was resized taller" - so the box ALSO tells the client, and
  the client shows or hides its own strip. The WM still owns the
  geometry, so the two halves stay in step (see the protocol below);
- grow box in the lower-right: two or three short lines, drawn by the WM.

**Resize from any edge.** The grow box is the *indicator*; the drag may
start on any frame edge or corner, as in OS 9. Kestrel hit-tests the
frame's border region on button-press, and the hit edge or corner
becomes the resize direction and the grabbed point. The existing drag
session (S4.1d: `gDrag*`, `dragTo`/`endDrag`, the grab, the quiet-end
rules) does the rest with a **mode** of move | resize. Motion applies the
delta to whichever edges the grab owns, against a minimum size; release
ends the session exactly as a move does.

**Coloration comes from the theme, not from Platinum.** The shapes and
states below are Platinum's; every colour is the Argentum design
language's, read from the `Theme` the toolkit already publishes — no
Platinum hex values anywhere. Kestrel is a toolkit client like any
other application, so it must take its palette from the same place they
do; a frame in foreign colours is the same class of mistake as a widget
in foreign colours.

| element | active | inactive |
| --- | --- | --- |
| frame outline | `chromeOutline()` | `state(Disabled).outline` |
| title bar | S4.1b accent tint (unchanged) | flat `chromeTop()` |
| title text | `text()` | `state(Disabled).label` |
| close / zoom glyphs | `chromeOutline()` | `state(Disabled).outline` |
| content background | `page()` | `page()` |
| grow box lines | `chromeOutline()` | `state(Disabled).outline` |

The accent tint on the title bar is the one piece of S4.1b that stays:
the frame's focus cue is the house accent, as it is elsewhere in the
language, and the title bar is not pinstriped. Everything Platinum
contributes here is shape and state, not texture. The inactive column is
the same shapes stepped down through the theme's disabled state, not a
second palette.

**Client geometry.** The client sits inside the frame, inset by the
frame's thickness on the sides and bottom — today it is inset by the
band only. A resize moves and resizes both: the frame to the new rect,
the client to the new content rect.

**Deferred, not decided:** window shading (double-click the title bar).
The inactive frame's cue is what S4.1b already does - the accent tint
falls away and the band goes flat - and no further desaturation is
specified.

**Protocol the client publishes (both are the client's to declare):**

- `_ARGENTUM_PREFERRED_SIZE` - the size the content wants, which the
  maximize button grows the frame to. Nothing in WM_NORMAL_HINTS carries
  it, and the toolkit knows it: its content view's `contentSize()`.
- the toolbar strip's height, so the WM reserves the right amount.

As built (`Window::setPreferredContentSize` / `setToolbarHeight`,
CARDINAL properties read by the WM at map time): the preferred size is
the CLIENT WINDOW size, so a window that carries its own strip declares
the size with the strip, and the zoom subtracts the strip's height when
the strip is hidden. The strip's height is the client's; the WM only
reserves it.

**Protocol the WM sends back (the toolbar state).** The frame's
show/hide-toolbar box is a message, not just geometry:
`_ARGENTUM_TOOLBAR`, a ClientMessage laid out like WM_DELETE_WINDOW
(`data.l[0]` = the atom, `data.l[1]` = the state, 1 = shown), and the
toolkit advertises it in WM_PROTOCOLS when the client declares a strip.
`Window::setOnToolbarToggle(cb)` receives the state and
`Window::toolbarVisible()` reads it; both sides start "shown", so the
message carries changes only. The WM sends it BEFORE the resize that
reserves or drops the strip's height, on the same connection, so the
client's single repaint (the Expose from that resize) already knows the
state and the strip never flickers. A client with no hook keeps its
strip and sees the old geometry-only behaviour.


### Edit plan (anchors verified against the tree, 2026-09)

`userland/kestrel/kestrel.cpp`:

- **240** `frame->init(… fh + BAND_H)` and **302**
  `XReparentWindow(dpy, m->client, frame->xid(), 0, BAND_H)`: the
  client's origin and size gain the frame's 1px side and bottom insets.
  **718** `XConfigureWindow(dpy, m->client, …)` places it again on a
  resize, so the content rect wants to be one helper rather than three
  literals that can drift apart.
- **432** `if (ly < 0 || ly >= BAND_H …)`: the button-press hit-test.
  The frame's border region joins it — a few px band along each edge and
  corner of the frame window — and a hit there sets a resize direction
  instead of starting a move.
- **326-330** the `gDrag*` globals, **473** `dragTo` (which moves the
  frame at **497** `XMoveWindow`), **505** `endDrag`: add a mode
  (move | resize), the grabbed edges, and the frame and client sizes at
  grab time. In resize mode `dragTo` resizes both by the delta against a
  minimum size instead of moving the frame. The grab, the quiet-end
  rules and the abuse guards are unchanged.
- **90-140** `FrameChrome::draw`: the band's control layout — close at
  the left, the title centred, the toolbar toggle at the right — and the
  grow box in the frame's lower-right.
- the frame's 1px outline: `XSetWindowBorderWidth` plus
  `XSetWindowBorder` on the frame window, adjusting the frame's own
  content origin (the border shifts the coordinate frame).

`userland/argentum/window.cpp`:

- **845** the `WM_DELETE_WINDOW` advertisement in `Window::init` is where
  the client already talks protocol; the same place publishes the
  client's hints. The preferred size is the content view's
  `contentSize()`, so publish from `setContentView` and after a resize
  rather than at init — there is no content view yet at init.

### As built (deviations, and what the slice uncovered)

The edit plan above was written against the pre-S4.3 tree; these are the
places where the code landed differently, and the two toolkit bugs the
slice uncovered.

- **The lip is 4px, and the client is inset by it.** The plan's "1px
  style side and bottom insets" and "a few px band along each edge" pull
  in opposite directions: with the client flush to a 1px outline there is
  no WM-owned strip to put the grips or the grow box in, and 1px is not a
  grab target. So `FRAME_PX = 4` is the frame's lip on the left, right
  and bottom (the client's content rect is `fw - 2*FRAME_PX` by
  `fh - BAND_H - FRAME_PX`), the 1px outline is the frame WINDOW's X
  border outside it, and the grow box is two short lines in the lip's
  lower-right corner. `BAND_H` is 20 (was 26). **X positions a bordered
  window by its OUTER corner**, so a frame created at (60,80) has its
  inside at (61,81) — the gate's pixels are derived from that.
- **`contentRect()` is one helper** (`clientRect`), used by the
  reparent, `applyFrameGeometry` (the one place frame geometry is
  applied: move, resize, zoom, toolbar) and ConfigureRequest.
- **Resize grips** = the lip on the left/right/bottom, the band's top
  `GRIP_PX` rows, and the 1px X border (a press on the border arrives
  with x/y outside the window). A press in the band **on a control** wins
  over a grip: the WM hands it to the toolkit so the `FrameChrome` runs
  the close/zoom/toolbar callback, and only then considers grips and the
  move. `bandControlAt()` is shared by the WM's press handler and the
  chrome's hit-test so the two cannot drift.
- **The frames select `SubstructureRedirect` too**, so a client's own
  Map/Configure requests land in the WM's hook and are answered on the
  frame's terms (a size request becomes a frame resize; the client is
  never moved behind the WM's back). Its own `applyFrameGeometry` comes
  back as a redirected ConfigureRequest, so the handler drops a request
  that already matches the client — that is what keeps it from
  re-entering.
- **Two toolkit bugs, found here and fixed here** (both latent until a
  frame had a border and a WM resized its client — neither is Kestrel's,
  and both are worth the lines because the failure modes were a WM crash
  and a silent geometry drift):
  - a window with a non-zero `border_width` also gets Exposes for its
    **border**, with coordinates outside the window (`x = -1`, or
    `x = width`). `Window::noteDamage` took them at face value, so
    `flushBacking` indexed the backing with them and walked past the
    pixman buffer — a wild write that page-faulted the WM. `noteDamage`
    now clips to the window and drops a rect entirely outside it.
    (`docs/reference/xfb-input-vs-upstream.md`-style note: nothing else
    in the toolkit ever had a border, so no window had ever seen one.)
  - the event loop resolved a ConfigureNotify's window through
    `ev.xany.window`, which aliases `XConfigureEvent`'s **event** field,
    not its `window` field. A frame gets SubstructureNotify
    ConfigureNotify for its reparented CLIENT, so the frame's toolkit
    window was resized to the client's geometry — its bands drifted by
    the lip and the toolbar box's hit-test missed by 8px. Only
    `window == event` resizes a window now.
- **The toolbar box speaks to the client** (`_ARGENTUM_TOOLBAR`,
  ClientMessage, `data.l[1]` = the state; `Window::setOnToolbarToggle` /
  `toolbarVisible()`; advertised in WM_PROTOCOLS when a strip is
  declared) *and* reserves the geometry — the message first, so the
  resize's Expose repaints once with the new state. krel_a proves it:
  `KREL-A-TOOLBAR off`/`on` and the strip's pixels go and come back (the
  gate checks the pixel where the strip was, that the state persists
  through a zoom, and the full round trip).
- **A window is never shown undressed** (S4.3b — found in use: "when a
  window is created there is a visible blanked area before it is
  drawn"). Every argentum window used the session/desktop colour as its
  X background, so a mapped-but-undrawn window flashed the wallpaper,
  and a frame was undecorated until Kestrel's loop handled its Expose.
  Now: toolkit windows carry the theme's *page* tone as their X
  background (a window surface — the toolkit's damage-rect backdrop fill
  still uses the session colour, so what an app DRAWS is unchanged);
  `Window::show()` paints once with the map when the map actually took
  effect (no WM case — under a WM the map is redirected and the Expose
  from the WM's map is the first paint); and Kestrel paints a frame's
  chrome — and the strip's — in the SAME server batch as its map, before
  the client's map, so chrome lands before any client pixel can be seen.
  Probe `krel_slow` (a window that sleeps 6s inside its first paint) plus
  the gate's `scr_s43_undrawn`/`_drawn` pair hold it: the frame is
  already decorated while the client paints, the client's own area is
  the theme's page with **zero** desktop-colour pixels, and the same
  pixel becomes the content once the paint lands.
- **The backing/segment is grow-only** (S4.3c — found in use: "when I
  try to resize the zoo window it crashes"). A resize drag re-allocated
  the whole backing *and* a fresh MIT-SHM segment per motion step; for a
  big window that is a multi-MB segment created and freed dozens of times
  a second, which starved the kernel's shm page mapper
  (`shm_map_page(): Oops, map_page() returned 0!` — the process then died
  without a diagnostic), the client's window went with it, and the
  server's teardown crashed in the shadow's close path (see
  `xfb-shadow-buffer-plan.md` §S1b — that second bug is fixed too). The
  toolkit now keeps the buffer while it still fits and grows it with
  slack (so a growing drag re-allocates O(log n) times), the SHM segment
  mirrors the backing's allocation (that is what keeps `XShmPutImage`'s
  stride matching, so the fast path stays on), and a resize marks the
  whole window damaged because the buffer survives it. Measured: the
  gate's whole run now allocates **13** segments where one-per-step was
  ~40, with zero `map_page` failures and zero page faults — the zoo
  resize reproduces clean (`.build/zoores_run.sh`, 4 hard legs, 3
  segments).
  *Residual (kernel) — **closed** (S4.3d, `mm/fault.c`):* a page fault the
  kernel cannot satisfy (a failed `map_page`/page-table allocation, so out
  of memory — there is no swap here) used to answer with `send_sig(SIGKILL)`
  and nothing that said *why*: uncatchable, unlogged as a reason, and some
  of the five user-mode sites had no message at all. Those paths now report
  the process, the pid, the address and the reason
  (`do_page_fault(): cannot map the page of process '...' (pid N) at 0x...
  - out of memory?`, rate-limited) and send **SIGBUS** — Linux's answer for
  "the page cannot be faulted in", and catchable, so a client can shut
  itself down cleanly. The two *kernel-mode* sites keep SIGKILL (they
  already report, and a kernel-side fault that the fault-recovering copy
  primitives could not absorb is a kernel bug, not something a handler can
  act on). Held by `userland/tests/oom_probe.cpp`, which eats memory until
  a fault cannot be satisfied, catches SIGBUS and exits from the handler —
  a SIGKILL would run no handler and print nothing, which is what makes
  the gate a real discriminator (§S4.3 gate: `.build/oom_run.sh` +
  `.build/oom_assert.py`, OOM-GATE-OK, 7 checks: the report, the
  underlying `map_page() returned 0`, the caught SIGBUS, the probe's
  clean exit, the session still usable afterwards, and the only fatal
  fault report naming the probe).
- **Deferred, not dropped** (decisions, not omissions):
  - **minimize** still needs the task list, and **shading**
    (double-click the title bar) is still undesigned; the inactive
    frame's cue stays "the accent tint falls away and the band goes
    flat".
  - `_NET_WM_MOVERESIZE` / `_NET_SUPPORTING_WM_CHECK`: not needed at
    all under this design — the WM owns the affordances, so no client
    ever asks to be resized.
