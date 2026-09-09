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

### S4.1c — WM_DELETE close
*Acceptance:* clicking the active frame's close glyph sends
`WM_DELETE_WINDOW`; the probe exits via `setOnClose` (logs
`S41C-CLOSE`) and Kestrel unmaps the frame (`KESTREL: unmanage`).

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

- **Move/resize of clients** (drag the title band; resize grips):
  S4.1 is decoration + focus per the milestone; a WM without user
  move/resize is still a WM for the acceptance. Revisit in S5 when the
  desktop chrome needs it.
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
