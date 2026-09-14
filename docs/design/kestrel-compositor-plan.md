# Kestrel as a compositing WM — findings (probe done, plan not written)

Status: **PROBE RESULTS (2026-09).** No plan yet; this records what the
de-risking probe established, so the plan starts from measurements.

Requested: "i was hoping to get a compositing wm out of kestrel".

## What already exists

- **Xfb has Composite, DAMAGE, DBE, Present and RENDER compiled in**
  (`userland/xfb/sources.txt`, and the symbols are in the built server).
  Xfb also has a **shadow buffer with a damage-driven flush to fb0**
  (`use_shadow`/`shadowMem`, `docs/design/xfb-shadow-buffer-plan.md`) —
  which is the compositor's transport, already built.
- **Kestrel has no compositing-manager code at all**: no `_NET_WM_CM_S0`,
  no redirect. The only "composite" in it is `Window::draw()`'s own
  client-side view compositing, which is unrelated.

So this is an application-level project, **not** the `hw/xfree86` port:
see the XAA/EXA history — acceleration in X is a hook layer over the
server's operation funnel, and Xfb is already the right base.

## What the probe established (`userland/tests/xcomp_probe.c`)

Run in the guest against the live desktop, via the **xcb** Composite
bindings — because the Xlib wrappers are **not built**: `libXcomposite`,
`libXdamage`, `libXrender` and `libXfixes` are all absent from
`third_party/x11` (only `libxcb-composite`, `libxcb-damage`,
`libxcb-render` exist, generated from xcbproto). Staging two of those,
`libX11-xcb` and `libxcb-composite`, was needed before the probe could
even load — the staging rule said they were "deliberately left out until
something links them", and now something does.

```
XCOMP-EXT: Composite present=1
XCOMP-CM: owner-is-us=1                 (the _NET_WM_CM_S0 claim works)
XCOMP-REDIRECT: accepted                (the server allows the redirect)
XCOMP-TREE: 4 child(ren) of the root
XCOMP-PIXMAP-OK: child 0x200001 -> pixmap 0x400003 pixel=0xf7f7f2
XCOMP-NAMEPIXMAP: OK
XCOMP-COST-READBACK: 1920x1080 = 14994 ms/frame
XCOMP-COST-BLEND: 200x150 over 1920x1080 = 0 ms/frame
```

Three things fall out:

1. **The mechanism works.** The redirect is accepted and
   `NameWindowPixmap` hands back a pixmap holding real window content
   (`0xf7f7f2` is the theme's page tone — the pixel that window painted).
2. **A compositor names the ROOT'S CHILDREN, never a client window.**
   Naming the probe's *own* window failed with **BadMatch**: under a
   running WM an ordinary window is reparented into a frame, so it is no
   longer a child of the root — which is precisely `NameWindowPixmap`'s
   BadMatch condition. The root's children *are* the WM's frames, and
   naming one succeeded. This is the shape the plan needs: **Kestrel is
   both the WM and the CM, so it composites its own frames**, and the
   client's pixels come with them.
3. **The client-side path is unusable, by measurement.** A full-screen
   `XGetImage` readback costs **~15 seconds per frame** (8.3 MB; ~0.5
   MB/s). That kills the readback-and-blend-in-pixman route outright —
   which was the only route available without Render. Compositing must be
   **server-side** (Render composite of the named pixmaps onto the
   overlay), which needs a Render client path: either vendor
   `libXrender` (MIT, small, and the natural fit for an Xlib toolkit) or
   drive `libxcb-render` directly.

Note the readback figure is so far outside plausible that it is worth its
own look separately — 8 MB in 15 s is ~500x slower than a slow socket
should be, so it is probably a small-chunk request/reply path rather than
raw bandwidth. It is recorded here as measured, not explained.

## Why acceleration belongs here

The compositor's per-frame work is N window pixmaps plus chrome, blended
over the whole screen — the canonical blit-and-blend workload, and exactly
what the parked `accel_ops { init, fill, blit, cursor, flip }` seam
(`docs/eval/gpu-accel-eval.md`, Q-G1/Q-G2) exists for. QEMU's `ati-vga`
implements a Rage128-class 2D engine, so it is testable.

## Open before a plan

- **Render client path**: vendor `libXrender` vs `libxcb-render`. Decide
  with the toolkit in mind (Argentum is core-protocol-only today:
  "XPutImage — no XRender/Xft").
- **The toolkit's rendering model**: window contents arrive as *server*
  pixmaps, while `drawImage()` takes a client-side `BitmapImage`. The
  compositor view needs a server-side surface primitive; the rest of the
  toolkit keeps rendering client-side. Two models, one seam.
- **Shadow/damage interaction**: the compositor writes a full screen per
  frame; the shadow's damage-driven flush is the transport (see the
  strip/freeze history for how subtle that drain is).
- **The ~15 s readback number**: understand it or rule it out.
