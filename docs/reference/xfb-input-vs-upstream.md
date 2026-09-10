# Xfb input vs other X.org servers (Xvfb, kdrive/Xephyr)

Reference note (2026-09). Compare FNX's `Xfb` input backend
(`userland/xfb/hw/xfb/fnxinput.c`) with the two upstream models it sits
between: **Xvfb** (the DDX it forked) and the **kdrive** family
(`Xfbdev`, `Xephyr`). Written while chasing the open input stall
(`docs/design/xfb-input-stall-finding.md`), so it leans on the two
questions that matter there: *who owns the device*, and *how does the
server learn there is input*.

Upstream sources cited are `xorg-server-21.1.4` (the tag FNX's fork
tracks): `hw/vfb/InitInput.c`, `hw/kdrive/src/kinput.c`,
`hw/kdrive/src/kdrive.h`, `hw/kdrive/ephyr/hostx.c`.

## 1. The three models at a glance

| | **Xvfb** (upstream) | **kdrive** (Xfbdev/Xephyr) | **FNX Xfb** |
|---|---|---|---|
| real input devices | **none** | per-driver, registered | 2 hardcoded (mouse, kbd) |
| device discovery | — | `KdPointerDriver`/`KdKeyboardDriver` lists, `NewInputDeviceRequest`, udev/hotplug | fixed paths (`XFB_MOUSE`/`XFB_KBD` env overrides) |
| per-device state | — | `KdPointerInfo` / `KdKeyboardInfo` (buttons, axes, matrix, emulation FSM) | none (two fds + `static` state) |
| hotplug | — | `NewInputDeviceRequest` / `DeleteInputDeviceRequest` | none |
| delivery API | — | `KdEnqueuePointerEvent` → `QueuePointerEvents` | `QueuePointerEvents` directly |
| pointer screen funcs | `vfbPointerCursorFuncs` | `kdPointerScreenFuncs` | `vfbPointerCursorFuncs` (same as Xvfb) |

## 2. Xvfb: no input at all

Upstream Xvfb's whole `InitInput()` is dummy devices plus `mieqInit()`:
`vfbMouseProc`/`vfbKeybdProc` call `InitPointerDeviceStruct`/
`InitKeyboardDeviceStruct` on `DEVICE_INIT` and otherwise just flip
`pDev->on`. There is no fd, no poll, no read. All Xvfb input arrives via
**XTEST** (or an XI client). `CloseInput()` is `mieqFini()`.

So "Xvfb input handling" is not a model to copy - it is the absence of
one. FNX's Xfb keeps Xvfb's *scaffolding* (`ProcessInputEvents` =
`mieqProcessInputEvents()`, the same `vfbPointerCursorFuncs`, the
`VFB_MIN_KEY`/`VFB_MAX_KEY` constants) but has replaced the empty middle
with a real backend.

## 3. kdrive: a driver per device

kdrive is the real-DDX model and the closest upstream analogue to what
FNX Xfb hand-rolled:

- **Registration**: `KdAddPointerDriver()` / `KdAddKeyboardDriver()`
  register named drivers (`KdPointerDriver` = `{name, Init, Enable,
  Disable, Fini}`). Each device instance is a `KdPointerInfo` /
  `KdKeyboardInfo` holding its buttons/axes/map/matrix and its driver.
- **Device lifecycle**: `KdPointerProc`/`KdKeyboardProc` implement the
  standard `DEVICE_INIT`/`DEVICE_ON`/`DEVICE_OFF`/`DEVICE_CLOSE` proc,
  calling the driver's `Init`/`Enable`/`Disable`/`Fini`. Xephyr's input
  driver is the host X connection; the (now removed) Linux kdrive drivers
  opened `/dev/input`-style nodes.
- **Delivery** (identical API to FNX's, one level up):
  `KdEnqueuePointerEvent(pi, flags, rx, ry, rz)` normalizes through
  `KdPointerMatrix` and `absrel`, then `_KdEnqueuePointerEvent()` →
  `QueuePointerEvents(pi->dixdev, type, button, absrel, &mask)`.
  Buttons are derived by diffing against `pi->buttonState`.
  Keyboard: `KdEnqueueKeyboardEvent(ki, scan_code, is_up)` converts a
  scancode into a keycode via `minScanCode`/`maxScanCode` and calls
  `QueueKeyboardEvents()`.
- **A mouse emulation state machine**: `KdRunMouseMachine()` implements
  middle-button emulation and click-hold/deliver/release with timeouts
  (`KdReceiveTimeout`).
- **Idle integration**: `KdBlockHandler()`/`KdWakeupHandler()` extend the
  server's wait with `AdjustWaitForDelay()` for those timers;
  `ProcessInputEvents()` = `mieqProcessInputEvents()` + `KdCheckLock()`.
- **Pointer screen funcs**: kdrive installs its **own**
  `miPointerScreenFuncRec kdPointerScreenFuncs = { KdCursorOffScreen,
  KdCrossScreen, KdWarpCursor }` - `KdCursorOffScreen` implements
  multi-head ("Zaphod") screen switching and returns FALSE for the
  single-screen case; `KdWarpCursor` wraps `miPointerWarpCursor` in
  `input_lock()/input_unlock()`.

## 4. FNX Xfb: Xvfb's shell + a hand-written fd backend

`fnxinput.c` (`vfbFnxInputInit`, called from `InitInput`):

- opens `/System/Devices/mouse` and `/System/Devices/PS2/Keyboard`
  `O_RDONLY | O_NONBLOCK`, `vfbSetRaw()`s the keyboard,
- registers each fd **once** with `SetNotifyFd(fd, notify,
  X_NOTIFY_READ, NULL)` (mouse ~line 453, keyboard ~line 466),
- the notify callbacks drain the fd and translate:
  `vfbMouseRecord()` → `QueuePointerEvents(vfbMouseDev, MotionNotify /
  ButtonPress / ButtonRelease, …, POINTER_RELATIVE, &mask)`;
  `vfbKbdRecord()` → `QueueKeyboardEvents()`.

Differences that matter:

- **The kernel owns the input semantics.** The devices already deliver
  *normalized* records: `/System/Devices/mouse` = 8-byte
  `{buttons, wheel, dx, dy, hwheel, pad}` (`drivers/char/mousedev.c`),
  `/dev/kbd` = 4-byte resolved-keysym records (`include/fnx/kbdaux.h`).
  So unlike kdrive - which receives scancodes and lets XKB map them -
  FNX Xfb receives keysyms and maps back to X keycodes, and receives
  signed deltas directly. No resync/framing lives in the server.
- **No driver abstraction, no hotplug, no emulation FSM, no pointer
  matrix/rotation.** Always relative; absolute/tablet not yet plumbed.
- **Pointer screen funcs are Xvfb's**, not kdrive's: `vfbCursorOffScreen`
  just `return FALSE`. Combined with X's MI default
  (`miPointerSetPosition` treats an out-of-range point as
  `point_on_screen()==FALSE` and calls `CursorOffScreen` for a
  screen switch), an **out-of-bounds pointer position** is what put us
  into the sprite code with a position off the screen - the hazard fixed
  in `bc2e3e4` by clamping the position in the backend.
- **No idle/wakeup hook for input.** The only custom block handler is the
  shadow-buffer flush (`xfbShadowBlockHandler` in `InitOutput.c`).
  kdrive has `KdBlockHandler`/`KdWakeupHandler`; Xephyr additionally
  keeps a `hostx_has_queued_event()` / saved-event path so a
  notification it did not act on is picked up later.

## 5. Where this leaves the open stall

`docs/design/xfb-input-stall-finding.md`: after a fast burst the server
keeps serving client requests but stops reading its input device. Both
upstream fd-based models differ from FNX Xfb in a way that is directly
relevant:

- kdrive/ephyr drivers **poll their source on notify** and can
  re-check for queued events (`hostx_has_queued_event`), i.e. they do not
  treat a single notification registration as the only trigger;
- FNX Xfb registers the fd once and relies entirely on that fd being
  reported readable.

So the two candidate directions (both consistent with the evidence that
the kernel still has data queued) are:

1. **Poll as well as notify**: drain the mouse device from a block
   handler / watchdog each server loop iteration (as Xephyr re-checks
   its host connection), so a missed notification cannot wedge input;
2. **Find out why the fd stops being reported**: instrument the wait set
   (`SetNotifyFd` → `ospoll`/input-thread registration) to see whether
   the mouse fd is still registered and readable at the stall.

Direction 1 is the kdrive-style robustness the backend currently lacks;
direction 2 is the root-cause hunt.
