# USB printer class support

Status: **PLAN (2026-09) — scoped, small; no code.** The USB printer
class (0x07) is the generic route to USB printers — a bulk-OUT
device, the USB twin of FNX's existing parallel-port driver. No QEMU
model (fixture/real-hardware verification), and no printing stack
exists yet — this is the transport driver ahead of the consumer, like
the USB-MIDI plan.

## 1. Grounding

- FNX has a parallel-port printer driver (`/dev/lp`-class) — a USB
  printer driver mirrors it: one bulk OUT endpoint carrying the
  printer's language (PCL/PostScript/ESC-P… the driver is
  language-agnostic; userland picks the format).
- Class 0x07 devices expose a bulk OUT (and usually bulk IN for
  status); control transfers carry the device-ID request
  (`GET_DEVICE_ID`: IEEE-1284 device string) and port status.

## 2. Design

- Enumerate class 0x07 interfaces; bind the bulk OUT endpoint; expose
  a `/dev/lp`-analog node (a second printer node if the parallel one
  exists — node arbitration mirrors the audio family's single-device
  reality, open item).
- **Device ID**: `GET_DEVICE_ID` at open/enumerate (the IEEE-1284
  string tells userland what language the printer speaks).
- Data path: bulk OUT writes of printer-language bytes; bulk IN read
  for status when present. No isoc, no class-specific protocol beyond
  the control requests.

## 3. Milestones

### UP0 — Enumeration + node
Class 0x07 interfaces recognized; bulk OUT bound; the lp-style node
registers.
**Acceptance**: a printer-class fixture enumerates to the node; a
non-printer device is ignored cleanly.

### UP1 — Data path
Bulk OUT writes; device-ID fetch.
**Acceptance**: fixture-driven writes complete exactly (byte count
matches); the device-ID string round-trips; a zero-length/edge write
is handled.

### UP2 — Real device + hardening
A real USB printer (or a fixture driving the same path) accepts a
job; unplug mid-write and malformed-descriptor handling are clean.
**Acceptance**: a job's bytes reach the device with no truncation;
unplug/re-plug re-enumerates; the fuzz battery never faults.

## 4. Out of scope (recorded)

- A printing stack (spooler, format selection) — userland, when a
  printing consumer exists; bidirectional status protocols beyond
  bulk-IN read; the parallel driver's job-arbitration details (node
  sharing is the open item above).
