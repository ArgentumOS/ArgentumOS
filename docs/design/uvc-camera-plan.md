# USB webcams — UVC (USB Video Class) support

Status: **PLAN (2026-09) — scoped; no code.** Every mainstream USB
webcam is **UVC** (USB Video Class, USB-IF standard 1.0/1.1/1.5) —
the standard is why one generic driver works everywhere. This plan
adds it to FNX. The **prerequisite** is isochronous endpoint support,
which the USB stack lacks entirely — that is the real unlock, and it
is camera-independent.

## 1. Grounding

- FNX HCDs (UHCI/OHCI/EHCI/XHCI) handle **control, bulk, interrupt**
  only. UVC streams over **isochronous** endpoints — missing across
  the stack.
- Enumeration today keys on interface class (HID, mass-storage);
  UVC devices expose **VideoControl** (class 0x0E) +
  **VideoStreaming** (class 0x0E, subclass 2) interfaces, ignored
  today.
- QEMU models **`-device usb-video`** — a UVC-class camera with a
  test pattern — so every milestone is verifiable in the QEMU loop
  (the usb-tablet story, for cameras).

## 2. The standard (why a generic driver works)

- A UVC device = **VC interface** (camera terminal + processing unit;
  class-specific requests `GET_CUR`/`SET_CUR` for exposure, focus,
  zoom, brightness) + **VS interface(s)** (frame descriptors: the
  supported formats × resolutions).
- Payload formats are standardized: **uncompressed YUY2/NV12**,
  **MJPEG**, and (1.5) H.264. The driver negotiates format/resolution
  over control, then receives frames on an isoc endpoint.
- MJPEG is the universal default — so frame *delivery* is the v1
  boundary; *showing* frames waits on a JPEG decoder FNX doesn't have.

## 3. Design

- **Isochronous first** (prerequisite, HCD-core work): XHCI isoc
  scheduling is the vehicle (modern, QEMU-native); UHCI/EHCI/OHCI
  isoc paths are later scope. A generic isoc-IN pipe API the UVC
  layer consumes.
- **UVC layer**: enumerate VC/VS interfaces; parse camera-terminal,
  processing-unit, and frame descriptors into a format × resolution
  table; negotiate over `GET_CUR`/`SET_CUR` (SELECT_ALT for the
  bandwidth altsetting); assemble isoc payloads into frames.
- **Capture node**: a `/dev/video0`-analog char device with a
  **house ioctl set** (format/resolution query+set, start/stop,
  frame fetch) — V4L2 is a Linux API, not something to import.
- **Verification path**: negotiate **uncompressed YUY2** (QEMU
  usb-video offers it) so acceptance is decoder-free; MJPEG is
  delivered raw until an image pipeline adds decode.

## 4. Milestones

### UV0 — Isochronous IN on XHCI
Isoc endpoint support: scheduling, the isoc-IN transfer path, frame-
list handling per XHCI's schedule model.
**Acceptance**: a QEMU `usb-video` isoc-IN stream delivers payload
data to a test consumer without scheduler corruption; existing
control/bulk/interrupt show zero regression.

### UV1 — UVC enumeration
VC/VS interfaces recognized; unit/terminal + frame descriptor parse
into a format table.
**Acceptance**: `usb-video` enumerates with its real format ×
resolution list (matches the host's `v4l2-ctl --list-formats` view of
the same QEMU device); malformed descriptors fail cleanly.

### UV2 — Negotiation + streaming
Control requests (GET_CUR/SET_CUR, altsetting select) negotiate YUY2;
isoc frames assemble.
**Acceptance**: streaming starts at a negotiated resolution; frames
arrive complete and contiguous; a checksum over a static test-pattern
frame matches the host capture of the same device.

### UV3 — Capture node
The char device + house ioctl set; frame fetch (read + a mapped
buffer path for later consumers).
**Acceptance**: a guest capture tool reads frames at the negotiated
format; frame boundaries are exact; stop/start is clean.

### UV4 — Hardening
Unplug mid-stream, negotiation fuzz, isoc transfer errors, altsetting
edge cases.
**Acceptance**: unplug during streaming is clean; the fuzz battery
never faults; re-plug re-enumerates; boot devices unaffected.

## 5. Out of scope (recorded)

- MJPEG/H.264 decode (raw MJPEG delivery only until an image
  pipeline exists); V4L2 API compatibility (house ioctl set); UAC
  audio (a separate standard/plan); UHCI/EHCI/OHCI isoc paths (XHCI
  first, others when a consumer needs them); camera quirks beyond
  QEMU's model.
