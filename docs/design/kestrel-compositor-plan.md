# Kestrel as a compositing WM — plan

Status: **PROPOSED (2026-09).** Decision requested before code starts.
Nothing here is built; §1 is measured, the rest is design.

Requested: "i was hoping to get a compositing wm out of kestrel" — Kestrel
redirects every top-level into offscreen storage and paints the screen
itself, so window effects (shadows, alpha, fades) become possible, and the
menubar's "above everything" rule becomes **structural** instead of
re-asserted per event.

## 1. Basis — what the probe settled

All of this is measured (`userland/tests/xcomp_probe.c`, run in the guest
against the live desktop):

| | |
|---|---|
| Xfb's extensions | Composite, DAMAGE, DBE, Present and RENDER are compiled in |
| Xfb's transport | a **shadow buffer with a damage-driven flush to fb0** already exists |
| Kestrel today | **no** CM code at all — no `_NET_WM_CM_S0`, no redirect |
| the redirect | accepted; the `_NET_WM_CM_S0` claim works |
| `NameWindowPixmap` | **works on a root child**, returning real content (`0xf7f7f2` is the theme's page tone that window painted) |
| `NameWindowPixmap` on a *client* window | **BadMatch** — under a WM the client is reparented into a frame, so it is no longer a child of the root |
| RENDER | a `Composite` over a redirected pixmap links and **executes** (`libXrender` vendored, 0.9.12) |
| the client-side route | **dead**: a full-screen `XGetImage` costs **~15 s/frame** |

The last row is why this is server-side compositing — that choice is
already made by measurement, not proposed.

Two facts the probe also fixed, both load-bearing below: naming a pixmap
works on **the root's children** (the WM's frames), and a client's pixels
arrive *with* its frame, because a child's drawing lands in its parent's
drawable.

## 2. Architecture (decided here)

- **Kestrel is the WM and the CM in one process and one connection.** It
  owns the frames, so it composites its own frames.
- **The target is the overlay window** (`CompositeGetOverlayWindow`), not the
  root. The server keeps the overlay above every other child — that is what
  makes the menubar rule structural. (The probe composited onto the *root*
  and the wallpaper covered it, which is the same lesson from the other
  side.)
- **Redirect `Automatic`**: windows keep drawing exactly as they do today and
  the overlay covers them. Nothing else in the desktop changes behaviour to
  be composited.
- **Composite = name, then blend**: per root child, `NameWindowPixmap` → an
  XRender `Picture` → `XRenderComposite` onto the overlay at that child's
  geometry.
- **Everything is uniform.** Wallpaper, strip, dock, frames and popups are
  all root children, so **one code path composites them all** — there is no
  "app window" special case.
- **Composite only what damaged** (C3). A whole-screen composite is ~8 MB of
  blit and Xfb's shadow then drains that to fb0; paying it per frame would
  pay the dominant cost twice.
- **The toolkit does not change for pass-through.** Kestrel keeps drawing
  chrome into its own windows exactly as today; the compositor blends their
  pixmaps. A server-side surface primitive is only needed if Kestrel ever
  draws *into* the overlay from server-side sources (§6).

## 3. The split

One acceptance per slice, observable in the guest, green before the next —
the rule the other plans here follow.

- **C0 — the overlay is ours. DONE (2026-09).** Kestrel takes the overlay at
  startup. *Acceptance as built:* the guest logs the take, and **every
  existing desktop gate stays green** (`wm_dock` 51/51).

  The unknown is answered: **Xfb DOES implement `GetOverlayWindow`.** Three
  measured facts reshaped the slice, and all three are load-bearing for C2:

  1. **The server maps the overlay as part of taking it**, so the desktop is
     hidden from that instant — an X window is not a transparent layer and
     there is nothing to see through until the compositor paints the screen
     into it. Measured: with the overlay up, the menubar's ink vanished from
     the framebuffer and every desktop pixel check failed *while the WM
     itself was perfectly healthy* (its clock kept ticking). So the overlay
     cannot be "taken now, painted later": C0 puts it away again with
     `XUnmapWindow`, and **C2 maps and paints it in the same breath**. The
     visual proof that it is above everything is therefore C2's, not C0's.
  2. **The overlay eats input** unless its input region is emptied
     (`XShapeCombineRectangles` with `ShapeInput` and no rectangles).
     Measured: with a normal input shape the dock's tile click never reached
     the dock, the app was never launched, and the desktop looked half-dead.
  3. **xcb on Xlib's connection must be preceded by `XSync`** — raw requests
     otherwise overtake the ordinary requests still sitting in Xlib's buffer.
     `libXcomposite` was then vendored so the compositor's Composite calls are
     ordinary Xlib alongside XRender, which retires this hazard: C2 mixes the
     two in one code path, and Kestrel no longer links xcb at all.

  Nothing of C0 is thrown away: the take, the input shape and the unmap are
  the first three things C2 needs.
- **C1 — claim the CM and redirect. BLOCKED on an Xfb memory fault
  (2026-09); the code is in, opt-in, and OFF.** `_NET_WM_CM_S0` is claimed
  and `CompositeRedirectSubwindows(root, Automatic)` is called, behind
  `system.workspace.compositor` (default off), because the redirect does not
  hold up. Measured over a long desktop session (`wm_dock`, which launches
  and closes many windows):

  ```
  Page Fault at 0x400014ed8000 (writing)  rip 0x4d8470     <- Xfb, OUT OF MEMORY
  Page Fault at 0xa (reading) x 11112     rip = OsSigHandler (os/osinit.c:138)
  ```

  Xfb exhausts memory with the redirect active — at 256M AND at 512M — and
  the reason is **measured, not guessed: it leaks the redirected window's
  pixmap when the window is destroyed.** Xfb's own `VmRSS` (read from
  `/System/Processes/4/status`, opening and closing a Widget Zoo window in a
  loop):

  | | Xfb VmRSS |
  |---|---|
  | compositor OFF, at ready | 20,264 kB |
  | compositor ON, at ready | 28,888 kB (**+8.6 M** = the wallpaper's own pixmap) |
  | ON, idle for 12s | 28,888 kB — flat |
  | each app launch + close | **+3,672 kB**, five cycles running |

  3672 kB is the Zoo window's pixmap (1248x744x4 = 3.7 MB) to the kilobyte.
  So the fixed cost is ONE pixmap, and the leak is one pixmap **per window
  destroyed** — which is why a long session (`wm_dock` opens and closes
  dozens: apps, menus, popups) dies while a short one (`smoke_desktop`) does
  not. The OOM's fault then drives the server into its own signal handler,
  whose `xorg_backtrace()` faults at 0xa in a loop (a ~70-line dump each,
  ~800k lines in the log); that cascade is what actually kills the desktop
  (late checks fail; the run takes 496s instead of 113s).

  **Next slice, precise:** `CompositeRedirectAutomatic` must free the pixmap
  in the window-destruction path — the `compFreeClientWindow` →
  `compDestroyWindow` → `FreePixmap` chain in `composite/compalloc.c` /
  `composite/compwindow.c`. Xfb's **shadow** is the one part of that code
  which is not upstream, so look there first.

  *Acceptance as built:* the claim and redirect exist and are opt-in; with
  the default off the desktop is exactly what it was, and `wm_dock` is 52/52.
  **The slice is NOT met** — its whole point was that redirecting alone is
  invisible, and it is not.

  Next, in order: (a) find where the memory goes — a per-window cost that
  large is worth measuring, and a leak worth ruling out, in Xfb's redirect
  path (`composite/compalloc.c`, and Xfb's **shadow**, which is the one thing
  there that is not upstream); (b) then re-run C1's acceptance with the
  redirect on.
- **C2 — pass-through composite.** Every root child is named and blended onto
  the overlay at its geometry, on damage. Pass-through is the strongest
  correctness test available: any mistake is a wrong pixel.
  *Acceptance:* the C1 sampled points are **still** identical, and the gates
  stay green.
- **C3 — bound the composite by damage.** Subscribe to DAMAGE on each
  redirected window and composite the union of the damaged rects rather than
  the screen. Needs a DAMAGE client path: vendoring `libXdamage` is the
  consistent choice — it is the same Xlib-vs-xcb argument that got
  `libXcomposite` vendored. *Acceptance:* the guest logs the
  composited area per frame, that area is far below full-screen during a
  drag, the sampled pixels are unchanged, and the per-frame cost drops
  against C2.
- **C4 — the first real effect: a frame shadow.** An alpha-blended shadow
  under each frame.
  *Acceptance:* pixels just outside a frame's lip change to the shadow tone
  while the frame's own pixels do not — both asserted, so the slice cannot
  pass by drawing the shadow over the window.
- **C5 — window alpha.** Inactive windows at reduced alpha (or a launch
  fade). *Acceptance:* a known blend value at a named pixel, the active
  window unchanged.
- **C6 — the gate.** The whole thing on one boot: the desktop composited, an
  app launched, moved and dragged, a menubar menu pulled down, all existing
  desktop gates green, zero fatal faults, clean shutdown.

## 4. Files

| file | what changes |
|---|---|
| `userland/kestrel/kestrel.cpp` | claim the selection + redirect at startup; call the compositor on damage (it already owns the frame list) |
| `userland/kestrel/compositor.cpp` (new) | the overlay, the per-child pixmap/Picture cache, the composite; later the effects |
| `mk/20-userland.mk` | link Kestrel against `-lXcomposite -lXrender` (and `-lXdamage` for C3); stage the themes/lib |
| `tools/x11-shared-build.sh`, `.gitmodules` | `libXcomposite` (done), `libXdamage` still to vendor for C3 |
| `tests/cases/` | the C0/C4/C5 pixel checks and the C6 whole-desktop case |

## 5. Risks, each with the experiment that settles it

- **Does Xfb implement `GetOverlayWindow`?** Unknown. "Compiled in" has
  already been false comfort once in this area — a client window naming its
  own pixmap gave BadMatch, and two unstaged libs stopped the probe from
  loading at all. C0 answers it on its first run.
- **A whole-screen overlay update through Xfb's shadow drain.** The drain is
  per-damage and the strip/freeze history shows how subtle it is; whether a
  full-screen overlay write is flicker-free is unmeasured. C2 measures it
  with the existing `xwinprobe` (shadow vs screen) *before* C3 optimises it.
- **The ~15 s readback.** Off the critical path now, but 8 MB in 15 s is
  ~500x slower than it should be and may be a general X-transport defect that
  bites elsewhere. Recorded, not adopted as a slice here.
- **Costs are TCG costs.** Every timing is emulation-inflated (~5–20x); the
  ratios between slices transfer, the absolute numbers do not.
- **The menu-above-everything machinery becomes redundant.** With an overlay,
  a pulled-down menu is a root child and is composited above everything by
  construction, so `_ARGENTUM_MENU` + the WM's re-raise (2026-09) loses its
  job. Keep it as belt-and-braces or retire it — a decision for C6, not
  before: until the overlay is proven, that machinery *is* the guarantee.

## 6. Deferred (decisions, not omissions)

- **Unredirecting a fullscreen window** — the standard performance trick when
  the compositor is invisible. Needs an "is anything above it?" answer first.
- **Occlusion tracking** (compositing only visible regions) — a bigger win
  than damage-bounding, and a bigger problem.
- **A server-side surface primitive in the toolkit** — only if Kestrel ever
  draws into the overlay from server-side sources instead of blending the
  pixmaps itself.
- **A 2D engine behind this.** The compositor is the canonical blit/blend
  workload — what the parked `accel_ops { init, fill, blit, cursor, flip }`
  seam (`docs/eval/gpu-accel-eval.md` Q-G1/Q-G2) exists for. It stays parked:
  this plan is CPU compositing, and the engine is a separate decision once
  there is a compositor to measure.
