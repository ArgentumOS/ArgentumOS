# Xfb: pointer input stalls after a fast burst (OPEN)

Status: **OPEN finding (2026-09).** Reproducible. The symptom was first
reported by the user as *"it freezes the GUI but not the whole system
(the debug console still works) when I drag a window"* after the live
drag work (S4.1) landed on the native input device.

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
