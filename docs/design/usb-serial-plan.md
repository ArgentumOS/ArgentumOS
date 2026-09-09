# USB serial — CDC-ACM class support

Status: **PLAN (2026-09) — scoped, small; no code.** CDC-ACM
(class 0x02, subclass 0x02) is the standard USB-to-serial
communication-device class — the generic USB serial port. FNX's
deep serial investment (consoles, real-hardware bring-up, the debugger
stub's dedicated UART) makes USB serial the natural next member of the
tty family. **QEMU models `-device usb-serial`** — dev-loop testable.

## 1. Grounding

- FNX's serial world: 16550A ttyS drivers, PCI-serial, polled-TX
  console support, and the kernel debugger's dedicated-UART design.
  A USB serial port is a `ttyUSB`-class node off the same tty layer.
- Bulk endpoints serve CDC-ACM (no isoc prerequisite); the
  house HCD set + bulk transport are done.
- Class specifics: CDC-ACM pairs a **communications interface**
  (class 0x02/0x02, with the abstract-control-management functional
  descriptor) with a **data interface** (0x0A). Control transfers
  carry **line coding** (`SET_LINE_CODING`/`GET_LINE_CODING`: baud,
  parity, stop bits) and modem-state bits; data flows on bulk.
- QEMU's usb-serial is a CDC-ACM device on a chardev backend —
  every milestone is verifiable in the loop.

## 2. Design

- Enumerate CDC-ACM pairs; bind the bulk data endpoints; expose a
  `ttyUSB`-class char node behind the existing tty/line-discipline
  surface.
- **Line control**: `SET_LINE_CODING`/`GET_LINE_CODING` (baud/parity/
  stop) on open/termios-set; modem-control bits (DTR/RTS via
  `SET_CONTROL_LINE_STATE`) as the tty layer expects.
- Data path: bulk OUT (host→device) and bulk IN (device→host)
  through the standard read/write/select machinery the other ttys
  use; **polled/IRQ discipline matching the family** (the IRQ-mask
  and drain lessons from the serial work apply).

## 3. Milestones

### UC0 — Enumeration + pairing
CDC-ACM interfaces recognized and paired (comm + data); functional
descriptors parsed.
**Acceptance**: QEMU usb-serial enumerates as a ttyUSB node; a
non-CDC device is ignored cleanly.

### UC1 — Line control
Line-coding get/set on open and termios change; control-line state.
**Acceptance**: opening the node sets the expected line coding
(baud/parity observed on the QEMU chardev side); termios changes
round-trip.

### UC2 — Data path
Bulk OUT/IN through the tty node; read/write/select; echo round-trip.
**Acceptance**: a guest loopback (write + read against the QEMU
chardev backend) transfers bytes exactly; a console-style session
over the USB serial node works.

### UC3 — Hardening
Unplug mid-transfer, line-coding fuzz, bulk error handling.
**Acceptance**: unplug is clean; re-plug re-enumerates; the fuzz
battery never faults; existing ttyS/PCI-serial show zero regression.

## 4. Out of scope (recorded)

- Vendor-class serial (FTDI/Prolific/CH340 private IDs) — per-vendor
  tables later if a real device demands one; the CDC-ACM standard is
  the generic route.
- ACM modem emulation beyond DTR/RTS; USB *gadget* (device-mode)
  serial — a different subsystem.
