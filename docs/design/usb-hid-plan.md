# More HID-class USB devices — a generic HID layer

Status: **PLAN (2026-09) — scoped; no code.** Today FNX handles two
HID devices: boot-protocol keyboard (class 3, proto 1) and
boot-protocol mouse (class 3, proto 2), both synthesized into the
existing psaux/scancode seams. Everything beyond boot protocol is
unhandled: consumer/media keys, wheel + extra-button mice, absolute
pointing (tablets/pen), gamepads. This adds the **generic HID** layer
that opens them.

## 1. Why

- Real desktop keyboards carry consumer keys (volume/play) the boot
  protocol never delivers.
- Modern mice are report-descriptor devices (wheel, >3 buttons);
  boot protocol is the 1980s floor, not the norm.
- QEMU's `usb-tablet` is an absolute HID device with no boot
  protocol — FNX cannot use it today; it is both a real gap and the
  perfect test vehicle.
- The HCD set (UHCI/OHCI/EHCI/XHCI) + interrupt pipes already work;
  what is missing is the descriptor-driven layer above them.

## 2. Grounding

- Enumeration keys on interface class: HID class 3 selects
  `usb_kbd_init()` (proto 1) / `usb_mouse`-equivalent (proto 2);
  everything else HID is ignored today.
- Interrupt endpoints are exercised (kbd/mouse poll via interrupt
  in) — report delivery has a working path.
- X/console input lands through the existing seams (keyboard →
  scancodes; mouse → psaux packets); new event kinds need a mapping
  into those or new char nodes.

## 3. Design

- **Report-descriptor parser**: fetch the HID descriptor (control
  `GET_DESCRIPTOR(HID)`), parse report descriptor items (short-item
  tags; main/global/local items; usage pages), build a usage table
  per report (id + usages + logical bounds). A small, self-contained
  component in the house style — a from-scratch minimal parser, not
  a Linux-hid-core import.
- **Input-event layer**: decoded input reports → typed events
  (key/relative/absolute/consumer), dispatched to:
  - the existing keyboard path for boot-compatible keys,
  - a consumer-key mapping (→ XF86 keysyms via the xkb data already
    in the system) for media keys,
  - new char nodes for what has no home today (absolute pointers,
    joystick axes/buttons).
- **Boot protocol stays**: usb-kbd/usb-mouse keep their boot path as
  the fallback; the generic layer engages when the report descriptor
  parses, and devices without a usable descriptor fall back exactly as
  today (no regression).

## 4. Milestones

### H0 — Report-descriptor parser
Fetch + parse report descriptors (short items, usage pages, report
ids); a unit-testable API over descriptor fixtures.
**Acceptance**: a fixture battery (captured descriptors from real
QEMU devices + curated samples) parses into the expected usage
tables; malformed descriptors fail cleanly.

### H1 — Decode + dispatch for keyboard-class and consumer keys
Parse input reports for keyboard-class devices; deliver consumer
usages (volume/play/…) as key events.
**Acceptance**: a keyboard fixture with consumer usages produces the
mapped key events; the boot keyboard path is byte-identical when the
descriptor is absent (fallback works).

### H2 — Mice: wheel, extra buttons, absolute pointing
Report-driven mice (wheel/buttons beyond 3); **absolute devices**
(usb-tablet) via a new absolute char node.
**Acceptance**: QEMU `usb-tablet` drives an absolute pointer (the
cursor tracks absolute position through the new node); a wheel mouse
reports wheel events; the existing psaux relative path is unchanged.

### H3 — Gamepads / joysticks
Multi-axis + button devices to a joystick-style char node
(axis/button reads, poll or interrupt).
**Acceptance**: a descriptor-fixture gamepad produces correct
axis/button events through the node; no X story needed (consumer is
later Argentum apps/emulation).

### H4 — Hardening
Unplug mid-report, descriptor fuzzing, report-id handling, polling
interval sanity.
**Acceptance**: unplug during an active read is clean; the fuzz
battery never faults the kernel; HCDs + boot devices show zero
regression under the existing battery.

## 5. Out of scope (recorded)

- Full Linux-hid parity (feature/output reports, exotic usage pages,
  vendor drivers) — only the input usages real FNX consumers need.
- XInput absolute-device plumbing in Xfb (tablets as X abs devices) —
  the char-node + later desktop integration is the boundary; an X
  abs path is a separate X-server item if a tablet consumer appears.
- Bluetooth HID (no BT stack).
