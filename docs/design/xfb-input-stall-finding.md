# Xfb: pointer input stalls after a fast burst (RESOLVED)

Status: **CLOSED (2026-09).** Root cause found and fixed in the xHCI
event ring - see "Resolution" at the end. The symptom was first reported
by the user as *"it freezes the GUI but not the whole system (the debug
console still works) when I drag a window"* after the live drag work
(S4.1) landed on the native input device.

**The X server was never at fault**, which is why every earlier
hypothesis here (wait-set loss, fd collision, record desync, the input
thread) turned out wrong: the device was simply never fed again.

## Symptom

While dragging a window, after a fast burst of pointer motion the GUI
stops responding to input entirely - no pointer motion, no clicks,
nothing reaches any client. The kernel is fine (the serial console keeps
answering), and the X server keeps serving *new* clients.

## Exact repro (headless, no organic input needed)

Gate `.build/s41u_run.sh` + `.build/s41c2_run.sh` style:

1. Boot the kestrel image with USB-only input
   (`-machine pc,i8042=off -device qemu-xhci -device usb-kbd -device usb-mouse`).
2. Park the pointer (40 x `mouse_move -127 -127`), find the title band in
   a screendump, move onto it, press, drag +n — **this works** and logs
   `KESTREL: move ... to <x,y>`.
3. Send a fast burst, e.g. `mouse_move +127,+127` x 30
   (`.build/s41a2_burst.py 30 127 127 <sock>`) - the pointer runs into the
   bottom-right corner.
4. Park again, aim at the band **computed from a fresh screendump**,
   press, drag — **nothing happens**: no `KESTREL: move`, the window does
   not move.
5. A new client still starts fine (`DISPLAY=:0 krel_b` prints
   `KREL-B-READY`).

Note: when designing a gate for this, **verify the press** (screendump,
recompute the band) — an unverified press after a burst looks identical
to the wedge but is just a miss.

## Evidence chain (what is and is not broken)

- **USB/input device: fine.** Kernel trace on every report showed 110/110
  reports delivered, including after the burst. `drivers/char/mousedev.c`
  traces showed `MOPEN count=1`, `MCLOSE` never, `MDROP` never - the
  device stayed open and queued every record.
- **X server: alive for requests.** After the freeze a new client
  connects, creates windows and becomes ready.
- **X server: stops reading its input device.** The `vfbDrainMouse()`
  trace stopped dead at the burst; the server never drained another
  record. No pointer events → no MotionNotify/ButtonPress to any client
  → the WM (Kestrel) sits in `XNextEvent` forever (its event-loop
  counter confirmed it is blocked, not spinning).
- **Last request processed before the stall**: `ConfigureWindow
  win=0x200001 mask=0x40` - a **CWWidth resize of the menubar strip**
  (a full-width 1280x30 window). `XW`/`XWa`/`XWz` markers showed that
  ConfigureWindow *completed*; the input drain stopped right after.
- **Not the input thread.** Rebuilding Xfb with `INPUTTHREAD 0` in
  `userland/xfb/include/dix-config.h` did not change the behaviour.
- **A separate real hazard, fixed** (commit `bc2e3e4`): the pointer
  position could leave the screen - the server's own pointer trace
  showed `(1406,926)` on 1280x800. Xvfb's pointer screen funcs (which
  Xfb also installs) make `CursorOffScreen` return FALSE, so no screen
  switch happens, but the sprite code still works on an off-screen
  position; the input backend now clamps to the screen and adopts the
  server's real starting position on the first record. This does not fix
  this stall.

## Remaining hypothesis

The server's input fd registration stops being serviced while client fds
keep working. Xfb uses kdrive `SetNotifyFd(vfbMouseFd, vfbMouseNotify,
X_NOTIFY_READ, NULL)` (`userland/xfb/hw/xfb/fnxinput.c`) to get the mouse
fd polled; the suspicion is that the fd is dropped from the wait set (or
the notify stops firing) around that window operation, so the fd is never
reported readable even though the kernel has data queued.

## To resume: instrumentation that works

- Write traces **unbuffered straight to fd 2** (init dup2s Xfb's fd 2 to
  the log file); the log's `ErrorF` output is buffered, so "the last line"
  there is *not* reliable during a wedge - this cost several cycles.
  Dump with `cat '/System/Variable Data/log/Xfb.log' | tail -400` over the
  guest's serial shell (the console still works).
- Useful probe points: the mouse drain (`fnxinput.c vfbDrainMouse`), the
  server's wait set (`os/WaitForSomething`/`ospoll`), `SetNotifyFd`, and
  `ConfigureWindow` (`dix/window.c`) with entry/exit markers.
- Compare, at the moment of the stall: does the kernel still have records
  queued (kernel trace) while the server never calls the drain (fd side)?

## Related

- Comparison with upstream models: `docs/reference/xfb-input-vs-upstream.md`
  (Xvfb has no real input; kdrive/ephyr poll their source on notify and
  re-check for queued events - a robustness the FNX backend lacks).
- Native input device work: commit `96263f4`
  (`docs/design/native-input-plan.md`).
- Pointer clamp: `bc2e3e4`.
- Live drags: `docs/design/argentum-s4-kestrel.md` (S4.1).

## Resolution (2026-09): the HID interrupt transfer was never re-armed

The stall was **not** in Xfb, the kernel input device, or the wait set.
It is an xHCI event-ring initialisation bug.

### Root cause

`xhci_ring_init()` appends a trailing LINK TRB to every ring it builds.
That is correct for the software-produced rings - the command ring and
the per-endpoint transfer rings - whose producer walks them and needs the
cycle flip that the LINK provides. The **event ring is not a software
ring**: the controller owns it, wraps it itself at `ERSTSZ`, and software
only ever reads it.

The event-ring consumer (`xhci_event_wait()`) walks slots sequentially
from `xhci->evt.enq`, so after `EVT_RING_TRBS - 1` = 127 events its index
lands exactly on that stale LINK TRB. The LINK has its cycle bit set, so
the consumer matches it, **consumes the LINK as if it were an event**,
and flips its CCS a full pass early. The controller is still on the old
cycle, so from then on every real event is rejected. Instrumentation:

    xhci: evt #112 idx=112 ccs=1 type=32   <- ordinary transfer events
    xhci: evt WRAP #127 ccs=0 wraps=1      <- consumed the LINK, wrapped early
    xhci: evt #128 idx=0 ccs=0 type=6      <- type 6 = LINK TRB, not an event
    xhci: poll #3072 enq=0 ccs=0           <- and never another event

The controller's transfer-completion event for the HID interrupt-IN is
therefore never seen, so `usb_mouse_cb()` never runs, so the interrupt
transfer is never re-submitted: **the pointer (and keyboard) go
permanently silent ~127 events after boot**, while the kernel, the X
server and the serial console continue normally. Xfb behaves correctly
throughout - it polls its fd, the kernel queue is empty (no records are
ever produced again), so nothing arrives.

That single mechanism explains the whole symptom set: "it freezes the GUI
while I drag" (a drag is what generates enough reports to cross the
ring), "fast drags drop the window", "click-and-hold reads as
click-and-release" (reports stop mid-click), and "the window follows the
moving mouse" (input only comes back on reboot). It also explains why the
earlier device-side trace showed a fixed number of reports and then
nothing, and why a *new client* could still connect.

### Fix

`drivers/usb/xhci.c`: after `xhci_ring_init(&xhci->evt, EVT_RING_TRBS)`,
zero the trailing slot (`memset_b(&xhci->evt.trbs[EVT_RING_TRBS - 1], 0,
sizeof(struct xhci_trb))`), with a comment recording why the LINK is
fatal there and necessary in the other rings.

### Evidence

Gate `.build/s41i_run.sh`: park + find the band + press + drag; burst into
the bottom-right corner; park again, fresh screendump, re-find the band,
**verified** press, drag twice.

- Before: only leg 1 logs `KESTREL: move ... to 120,120`; the trace ends
  with the LINK consumption above and `enq=0 ccs=0` forever.
- After: all three legs move - `to 120,120`, `to 180,160`, `to 220,180`
  (each the exact expected delta) - the post-wrap event is a real
  `type=32`, and the held button is carried through the drag
  (`btn=01 dx=40 dy=20`).

### Alongside: whole-record queueing (latent, fixed while here)

Both record producers (`mousedev_event()`, `kbdaux_event()`) inserted
their fixed-size record with byte-at-a-time `charq_putchar()` calls and
ignored the return value. `charq_putchar()` returns `-EAGAIN` when the
1024-byte queue is full, so a burst that outran the reader could leave a
*partial* record in the stream - shifting every later 8-byte (or 4-byte)
record and feeding the reader garbage, i.e. exactly the fabricated
button edges and wild deltas this device exists to eliminate. Both now
check `charq_room()` first and drop the **whole** record when it does not
fit (incrementing `dropped`, warning on the first and every 128th), so
the stream can never be torn.

Note for future instrumentation: Xfb's fd-2 traces land in
`/System/Variable Data/log/Xfb.log` (init dup2's fd 2 to the file), and
that file **accrues across boots** - dump a filtered view, not `tail -N`.
Kernel `printk` goes to the serial console directly and is far easier to
read.
