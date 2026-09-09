# USB MIDI — MIDIStreaming (MS) class support

Status: **PLAN (2026-09) — scoped, small; no code.** USB MIDI
controllers, keyboards, and interfaces are the **MIDIStreaming (MS)**
class — part of the USB Audio Class family (class 0x01, subclass
0x03). The pleasant fact: **USB-MIDI uses bulk endpoints, not
isochronous** — no UAC/UVC-style HCD prerequisite; the existing bulk
transport serves it. Consumer is future Argentum audio/music apps; no
kernel MIDI consumer exists today.

## 1. Grounding

- MS interfaces pair with an AudioControl interface (a MIDI device
  usually also carries an audio-streaming side) and expose **bulk OUT
  + bulk IN** endpoints carrying USB-MIDI event packets.
- Bulk transport works across all HCDs today — this driver needs no
  new HCD feature (contrast the UAC audio plan's isoc prerequisite).
- QEMU models **no USB-MIDI device** — verification is descriptor/
  event fixtures + real hardware, not the QEMU loop.

## 2. The standard (essentials)

- The MS interface's descriptors describe **jacks** (input/output
  jack descriptors + associated element descriptors) and the
  endpoint's embedded-jack topology — v1 can treat jacks as a light
  pass-through (which endpoint carries events) rather than a full
  routing model.
- Wire format is the **USB-MIDI Event Packet**: a 4-byte unit
  (cable number + code index number (CIN) + 3 MIDI bytes) — or four
  32-bit packets per 16-byte "USB-MIDI packet" in the extended form.
  The driver de/encodes CIN-tagged MIDI messages to/from raw MIDI
  bytes.

## 3. Design

- Enumerate MS interfaces (class/subclass); identify the bulk OUT/IN
  endpoints; decode inbound event packets to **raw MIDI bytes** and
  encode outbound bytes into event packets.
- **Surface**: a `/dev/midi`-analog char node carrying raw MIDI
  bytes + a small house ioctl set (device/cable info, buffer
  control) — the kernel does transport, not music: no timing
  scheduling, no synth, no sequencer in the kernel (those are
  Argentum-side apps).

## 4. Milestones

### MM0 — Enumeration + endpoint binding
MS interfaces recognized; bulk endpoints bound; jack descriptors
parsed lightly.
**Acceptance**: an MS interface fixture enumerates with its endpoints;
a non-MS interface is ignored cleanly.

### MM1 — Event packet transport
Inbound 4-byte packets → raw MIDI bytes on the node; outbound bytes →
packets; cable/CIN handling.
**Acceptance**: fixture-driven packet streams decode to the exact
MIDI byte sequences (and encode back); partial/fragmented packets
reassemble correctly.

### MM2 — Real-device + hardening
A real USB-MIDI keyboard/interface round-trips (note on/off) through
the node; unplug mid-stream and malformed-packet fuzzing are clean.
**Acceptance**: live note events arrive on the node byte-exact;
unplug/re-plug re-enumerates; the fuzz battery never faults.

## 5. Out of scope (recorded)

- Jack routing models beyond pass-through; UAC2 MS variants in v1;
  sample-accurate MIDI timing, a kernel synth, or a sequencer — the
  kernel transports events; music happens in userland apps that do
  not exist yet (this plan is the cheap plumbing ahead of the
  consumer, not a feature with a present user).
