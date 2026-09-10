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
- **Session socket + config framing (§6).** Kestrel listens on an
  AF_UNIX socket under the FSH temporary-files convention (the X11
  sockets' home, e.g. `/System/Temporary Files/argentum-session`).
  Apps connect at map time and publish their menu model as config-
  framed records: menus/titles, item kinds (action/check/radio/
  separator), enabled state, keyboard equivalents, item ids. Picks
  flow back as triggers (id -> app); model diffs (enable/disable/
  relabel) can follow the same records. The toolkit gains a small
  serialize/parse pair over its `Menu`/`MenuItem` model plus the
  client-side connect/publish helper; Kestrel deserializes into the
  same model for its bar.
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

### S4.2a — session socket: publish + menubar render
*Acceptance:* probe A (and B) publish distinct menus (File/Edit …)
over the session socket; Kestrel's menubar strip renders the focused
app's menu titles in the bar chrome (screendump + a Kestrel log of the
parsed tree).

### S4.2b — picks + focus swap (whole S4)
*Acceptance:* clicking a bar title drops its items; picking an item
routes the trigger back to the owning app and its `MenuItem` action
runs (the app logs it). Switching focus between A and B re-renders
the bar with the newly focused app's menus. Original S4 acceptance:
two apps, the bar swaps with focus, picks trigger app actions.

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

- **Move is DONE** (S4.1d, title-band drag); **client resize** (edge/
  corner grips) remains deferred until S5.
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

*Status: DESIGN (2026-09), decided; implementation is the next slice.*
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
- the title band's controls, in Mac OS X order (decided): **close,
  minimize, maximize** at the left in that order, the title **centred**,
  and a **show/hide toolbar** button at the right. This supersedes the
  Platinum close-box/zoom-box arrangement: the frame takes its *shapes*
  from Platinum and its *controls* from OS X, which is the
  Snow-Leopard-parallel line the catalog was cut along;
- **maximize means grow to fit the content**, not fill the screen. The
  client publishes the size it wants - the toolkit knows it, it is its
  content view's `contentSize()` - and the WM's button resizes the frame
  to that. One new piece of protocol: an atom the toolkit sets and
  Kestrel reads, since nothing in WM_NORMAL_HINTS carries "the size this
  content wants";
- **minimize** has nowhere to go yet: there is no Dock and no task list,
  so iconifying today would make a window vanish with no way back;
- **show/hide toolbar** implies the frame has a toolbar area to toggle.
  OS X attaches it to the title band; nothing draws one here yet, so the
  button has nothing to act on until that strip exists;
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

**Deferred, not decided:** window shading (double-click the title bar),
and desaturating the frame when inactive beyond dropping the pinstripes.

**Open — decide before those buttons can be wired:**

1. **Where does a minimized window go?** Kestrel has neither a Dock nor a
   task list, so iconify needs a destination: a strip Kestrel draws along
   a screen edge, a window list in its own UI, or the minimize button is
   deferred until one of those exists.
2. **Who owns the toolbar?** On OS X it is part of the window's title
   area and the *client* fills it. Here the frame is the WM's and the
   client is a separate reparented window, so it is either a strip the WM
   reserves and the client draws into, or the client's own and the button
   merely tells it to show or hide it.
