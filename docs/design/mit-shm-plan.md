# MIT-SHM on Xfb — zero-copy client→server image transport

Status: **PLAN (2026-09) — server half already present; M0 (proof) is
the next step.** Related: `docs/design/x11-xvfb-fb-plan.md` (Xfb),
`docs/design/argentum-uikit-plan.md` (the consumer),
`tools/x11-shared-build.sh` (the X stack build).

## 1. Goal

Remove the client→server **socket copy** of bulk image payloads on the
X11 wire by enabling the **MIT-SHM** extension: the client puts pixels
in a SysV shared-memory segment and sends only the segment id; the X
server attaches the same memory and draws from it. The one remaining
copy is the server's own software blit (pixmap → /dev/fb0), which is
not an X11-copy and stays.

## 2. Ground truth (verified in the tree)

- **Server half is compiled in**: `userland/xfb/Xext/shm.c` is the full
  extension; registered at `userland/xfb/mi/miinitext.c:114`
  (`{ShmExtensionInit, "MIT-SHM", &noMITShmExtension}`).
- **Runtime-gated by a kernel probe**: `Xext/shm.c:178` calls
  `shmget(IPC_PRIVATE, 4096, IPC_CREAT)` at init and prints
  "MIT-SHM extension disabled due to lack of kernel support" if the
  probe fails. FNX's SysV shm works, so the probe should pass — Xfb is
  probably **already advertising MIT-SHM, never exercised by a client**.
- **Client wire lib is staged**: `libxcb-shm.so` is in
  `.build/x11-prefix/lib`.
- **High-level Xlib API is not staged**: `XShm*` lives in
  **libXext**, which the X stack build does not produce — and the
  Argentum UIKit is an Xlib client, so libXext is the missing client
  link.
- The UIKit's current image path is core-protocol `XPutImage`
  (socket-copied); S0.2/S1.x verified fills and text through it.

## 3. Milestones

### M0 — Prove the server answers MIT-SHM
A tiny guest client: query the extension, create a segment,
`ShmPutImage` a known pattern into a window, over the live desktop.
**Acceptance**: the pattern lands on the framebuffer (screendump
pixel check); the server log shows the extension enabled (no
"disabled due to lack of kernel support"); zero kernel errors.
Status: **DONE** (`userland/tests/xshm_m0.c`, commit f0429a0). The
probe was first written against `libxcb-shm` but xcb could never
connect to the FNX X socket (xcb's abstract-then-filesystem probe
order differs from Xlib's transport, and DISPLAY=unix/... full-path
form also failed) — M0 was redone on the XLIB path (the real client
transport) with libXext. Two server-side facts surfaced:
- The segment mode must be 0666, not 0600: FNX has no SO_PEERCRED,
  so Xorg's `shm_access()` falls through to the "other" bits and
  0600 denies (BadAccess).
- Gate `.build/shmm0_run.sh`/`shmm0_assert.py` -> SHMM0-OK: two-band
  400x300 window painted via XShmPutImage, screendump probes green.

### M1 — Stage libXext
Add libXext to `tools/x11-shared-build.sh` (shared, into
`.build/x11-prefix`), so `XShmCreateImage`/`XShmPutImage`/`XShmAttach`
are linkable for Xlib clients (UIKit, future toolkit apps).
**Acceptance**: `libXext.so` + `X11/extensions/XShm.h` staged; a
guest `XShmQueryExtension` client links and reports the server's
MIT-SHM present. Rebuild chain per the doc's own notes.
Status: **DONE** (commit f0429a0) — libXext 1.3.6 vendored at
`third_party/x11/libXext` (untracked, like libXau/libXdmcp), built by
`tools/x11-shared-build.sh` (`au Xext libXext`) with the `--host`
cross configure; `mk/20-userland.mk` stages `libXext.so.6` into
System/Libraries. Note: 0600 segments are refused server-side (see
M0) — XShm segments must be created 0666.

### M2 — Adopt in the UIKit image path
Switch the UIKit's image drawing from `XPutImage` to `ShmPutImage`
for payloads above a small threshold (tiny draws keep the core path —
segment setup costs more than a small copy).
**Acceptance**: UIKit large-image draw through MIT-SHM renders
byte-identical to the `XPutImage` path (guest screenshot compare);
the X11 wire carries segment ids, not image bytes, for the shm path.
Status: **DONE** — `Window::flushBacking()` uses a persistent per-
window SysV segment + `XShmPutImage` when the server answers XShm and
the backing layout is unpadded (stride == width*4); otherwise the
`XPutImage` core path stays. Segment mode 0666 (M0's SO_PEERCRED
finding). Two bugs found on the way:
- The `XShmSegmentInfo` handed to `XShmCreateImage` becomes the
  XImage's `obdata`, read again by `XShmPutImage` at EVERY flush — it
  must live as long as the window (a function-local dangles -> the put
  sends a stale segment id and the server answers BadShmSeg).
- `XSync` after `XShmAttach` (the xcb-route attach must not overtake
  buffered Xlib requests).
Gates: zoo session SHADOW-OK (pixels identical, server-side SHM-PUT
trace showed seg id + offset 0 before the trace was stripped) and
S13-PIXELS-OK (theme_chrome through the shm path).

## 4. Caveats (stated, not hidden)

- **Trust domain**: MIT-SHM hands the X server a client-writable
  segment — the standard X11 model (the server already trusts clients
  absolutely; there is no per-client isolation in X11). Cross-principal
  note: session apps (Admin) and Xfb (Display) share the segment by
  SysV permission; consistent with X11's trust posture, not a new one.
- **Lifecycle**: the server attaches on `ShmAttach` and detaches on
  `ShmDetach`/connection close; error paths (client dies mid-frame)
  rely on the existing shm cleanup. Watch segment-size caps
  (`shm_cap_test` exists) against large windows.
- **The win is bounded**: it removes the client→server copy; the
  software blit to fb0 remains. The payoff is animation and
  full-window updates, not the last copy.

## 5. Open

- Whether `libXext` must also be added to the recovery/linter sets or
  stays desktop-only (it is a client lib — likely shared-stack only).
- Segment size policy for large windows (2MB-class caps) once M0
  measures real attach sizes.
