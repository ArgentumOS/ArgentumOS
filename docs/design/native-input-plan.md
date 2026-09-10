# Native input devices — real HID delivery, no PS/2 emulation

Status: **N0 + N1 DONE (2026-09); N2 (keyboard) next.**
`drivers/char/mousedev.c` is the native mouse device (`/System/Devices/mouse`
by role alias, `PS2/Mouse` or `USB/Mouse` by topology); the 8042 port decodes
its 3-byte packets into records (`psaux.c`), `usb-mouse` decodes its HID
report into records, and `psaux_synth_packet` + the raw `/dev/psaux` byte
device are gone. Xfb reads records. Gate `.build/s41u_run.sh` (USB-only:
`pc,i8042=off` + `qemu-xhci` + `usb-kbd` + `usb-mouse`) drives a real window
drag through the whole path; `make run-*` now defaults to that configuration.

Today FNX's input devices
are half-native: the keyboard has a real abstracted device (`/dev/kbd`,
4-byte resolved-keysym records), but the **mouse has none** — it is only
a raw 3-byte PS/2 stream (`/dev/psaux`), and both USB HID drivers
emulate PS/2 to reach it (usb-kbd → set-1 scancodes; usb-mouse →
synthesized PS/2 packets). This plan gives USB HID devices a
**first-class delivery path** in the house idiom, and removes the
emulation seams. It re-parents the delivery half of
`usb-hid-plan.md` (that plan's feature backlog — wheel, consumer keys,
tablets, gamepads — is blocked behind this).

## 1. Why

Three defects, in increasing order of consequence:

- **Truth.** A USB keyboard registers as `PS2/Keyboard` and a USB mouse
  as `PS2/Mouse` (`drivers/char/kbdaux.c`, `drivers/char/psaux.c`): the
  device tree mislabels the bus it is on.
- **Fidelity.** The 3-byte PS/2 packet is a 1980s wire format. It has no
  encoding for a wheel, buttons beyond three, consumer/media keys, or
  absolute pointing. Every feature in `usb-hid-plan.md` §1 is blocked
  behind emulating a device the hardware is not.
- **Robustness.** A raw 3-byte stream needs framing and resync
  heuristics, and a lost byte misaligns it (the sync bit does not
  survive arbitrary data). A synthesized stream that can be interleaved
  with controller bytes can desync into **fabricated button flips** —
  the reported "press registers as press+release" symptom. A native
  record device is self-framing and cannot desync.

## 2. Grounding

- `/dev/kbd` (`include/fnx/kbdaux.h`) is the **template**: a heap-state,
  open-count-gated byte queue of fixed 4-byte records `{key, mods, state,
  pad}`, carrying *normalized* events. Its own header already states the
  intent: *"the kernel keyboard pipeline (PS/2 IRQ1 and USB-HID alike,
  both through keyboard.c) delivers normalized key-press events here."*
  The mouse is simply missing the analogous device.
- `/dev/psaux` (`include/fnx/psaux.h`) is a raw PS/2 byte queue. It is
  the only mouse seam, so USB HID re-encodes into it
  (`drivers/usb/usb-mouse.c` → `psaux_synth_packet`).
- `usb-kbd.c` maps HID usage → set-1 scancode → `keyboard.c` scancode
  keymap → keysym record: the keysym is native, but the route borrows
  the PS/2 scancode table.
- `fsh-proposal.md` Q3 already fixes the naming: the topology tree
  (`USB/Keyboard`, `PS2/Mouse`) is the real namespace; role views
  (`Devices/Keyboard`, `Devices/Mouse`) are alias dirs — the same
  pattern devfs uses for `/dev/mouse`. The `@mouse` shorthand resolves a
  role name.
- The only consumer of both seams is Xfb
  (`userland/xfb/hw/xfb/fnxinput.c`): `vfbFeedMouseByte()` (3-byte
  parser) and the `/dev/kbd` record reader. The migration is contained.

## 3. Design

1. **`mousedev` — the native mouse device** (sibling of `kbdaux`): a
   heap-state, open-count-gated queue of fixed records carrying
   *normalized* pointer events. Proposed record (D1):

   ```c
   struct mouse_event {         /* 8 bytes */
       unsigned char buttons;   /* bit0 L, bit1 R, bit2 M, ... */
       unsigned char wheel;     /* signed, vertical clicks */
       short dx;                /* signed relative X */
       short dy;                /* signed relative Y */
       unsigned char hwheel;    /* signed, horizontal clicks */
       unsigned char pad[3];
   };
   ```

   Relative deltas stay relative (a PS/2 or boot mouse is a relative
   device); absolute pointing (tablet) is a separate event kind or node
   (D4).

2. **One normalized producer path.** Both sources decode to records:
   - **PS/2 (8042):** `psaux.c` decodes its 3-byte packets in the kernel
     into records — the PS/2 mouse becomes a *decoded producer*, not a
     wire format every consumer must parse. The raw `/dev/psaux` device
     may remain for debug/legacy (D2).
   - **USB HID:** `usb-mouse.c` parses the report descriptor/report and
     emits records directly. **`psaux_synth_packet` is deleted** — this
     is the emulation the user rejects.

3. **Keyboard, for real.** `usb-kbd.c` maps HID usage → keysym through a
   HID-usage keymap in `keyboard.c` (a new table, the analogue of the
   scancode map), feeding the same `kbdaux` records. The console still
   needs scancodes for its own keymap, so the console emission maps
   usage → scancode (a small table) while `/dev/kbd` gets the keysym
   (D3). No set-1 scancode table sits in the middle of the HID path.

4. **Device tree by bus + role.** Input devices register at their real
   topology paths — `USB/Keyboard`, `USB/Mouse`, `PS2/Keyboard`,
   `PS2/Mouse` — and expose **role aliases** `Devices/Keyboard` /
   `Devices/Mouse` (the FSH Q3 pattern). Xfb opens the **role alias**,
   so the desktop is bus-agnostic; `@mouse` resolves the role.

5. **Xfb** reads records from the role-alias mouse node (the
   `vfbFeedMouseByte` 3-byte parser is replaced by a fixed-record
   reader). No PS/2 framing is parsed anywhere in userspace.
6. **Xfb maps the wheel to X core buttons.** A record's `wheel` /
   `hwheel` notches become momentary button 4/5 (vertical) and 6/7
   (horizontal) press+release pairs — the shape every X client expects
   a pointer wheel to take, and what the Argentum toolkit's
   `View::mouseWheel` consumes. A record is clamped to 3 notches per
   axis so an over-fast read cannot flood a client, and the record's
   button bitmask still carries only the three real buttons, so a
   notch never disturbs button state. (QEMU's `usb-mouse` has no
   horizontal pan, so only 4/5 are exercised in QEMU.)

## 4. Decisions (decided 2026-09)

- **D1 — mouse record: DECIDED, 8 bytes**
  `{buttons:u8, wheel:i8, dx:i16, dy:i16, hwheel:i8, pad[3]}`.
- **D2 — `/dev/psaux`: DECIDED, retire it.** The PS/2 mouse appears only
  as decoded records; the raw 3-byte byte interface and
  `psaux_synth_packet` are removed.
- **D3 — console keyboard keymap: DEFERRED** (keyboard is a later
  pass).
- **D4 — scope: DECIDED, mouse only now (N0–N1).** Relative pointer +
  wheel/extra buttons via the record's fields; absolute/tablet and
  consumer keys stay in `usb-hid-plan.md` H1/H2. Keyboard (N2) next.
- **D5 — naming: `PS2/Mouse` + `USB/Mouse` topology nodes with a
  `Devices/Mouse` role alias** (Xfb opens the role alias).

## 5. Milestones

### N0 — `mousedev` device + PS/2 producer  [DONE]
The native mouse record device exists; the 8042 path decodes into it;
Xfb reads records from it (relative motion + 3 buttons at parity with
today).
**Acceptance**: the existing drag/pointer gates pass unchanged with Xfb
reading `mousedev` instead of parsing `/dev/psaux`; `/dev/psaux` is no
longer opened by the server.

### N1 — USB HID mouse, for real  [DONE]
`usb-mouse` emits records from the HID report; the PS/2 synthesis is
deleted; device tree registers `USB/Mouse` + the `Mouse` role alias.
**Acceptance**: a USB mouse drives the pointer with zero PS/2 encoding
(no `psaux_synth_packet` in the tree); a wheel + 5-button HID mouse
reports wheel/extra buttons; `Devices/Mouse` resolves the USB node; the
PS/2 path (N0) still works and is the fallback when no USB mouse exists.

### N2 — Keyboard, for real
`usb-kbd` maps HID usage → keysym via the HID-usage keymap; `USB/Keyboard`
+ the `Keyboard` role alias; `@` shorthand resolves role names.
**Acceptance**: typing over USB produces the same `/dev/kbd` records as
PS/2 (byte-identical sequences for the shared key set); the console key
path is unregressed; a USB keyboard no longer registers as `PS2/Keyboard`.

### N3 — Feature depth (wheel/consumer/absolute)
Per `usb-hid-plan.md` H1/H2, now that the delivery seam exists: consumer
keys → keysyms; absolute pointing (`usb-tablet`) via its own node.
**Acceptance**: the usb-hid-plan H1/H2 acceptance criteria, met through
the native devices.

### N4 — Hardening + bus-less registration
Unplug mid-report; descriptor fuzzing; a machine with **no** PS/2
controller (QEMU `pc,i8042=off`) must still register the input devices
and its USB devices must drive the desktop.
**Acceptance**: `pc,i8042=off` boots to a working desktop with a USB
keyboard + mouse; unplug during an active read is clean; no regression
in the existing input battery.

## 6. Out of scope (recorded)

- Gamepads/joysticks and HID fuzzing stay in `usb-hid-plan.md` H3/H4.
- Bluetooth HID (no BT stack).
- XInput absolute-device plumbing in Xfb beyond what N3's node needs.
