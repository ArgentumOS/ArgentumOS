# USB audio — UAC (USB Audio Class) support

Status: **PLAN (2026-09) — scoped; no code.** USB headsets, speakers,
and microphones are **UAC** (USB-IF Audio Class 1.0/2.0/3.0) — the
audio twin of the UVC camera standard. Shares the **isochronous
prerequisite** with `docs/design/uvc-camera-plan.md` (audio adds
isoc-*OUT*: playback); attaches to FNX's established OSS `/dev/dsp`
audio surface.

## 1. Grounding

- FNX audio drivers (AC97, ES1370, HDA, SB16, GUS, virtio-snd) all
  expose an OSS-style dsp node — a USB audio driver joins the same
  family with the same house interface.
- HCDs handle control/bulk/interrupt only; UAC streams over
  **isochronous** endpoints (OUT for speakers, IN for mics) — the
  missing stack piece, shared with the UVC plan.
- QEMU models **`-device usb-audio`** (a UAC 1.0 device on the QEMU
  `-audiodev` backend) — dev-loop verifiable.

## 2. The standard

- A UAC device = an **AudioControl (AC)** interface (input/output
  terminals, feature units — volume/mute — and in UAC2 clock
  entities) + **AudioStreaming (AS)** interface(s) with format
  descriptors (sample rate, bit depth, channels) and isoc endpoints.
- Control is the same class-specific request pattern as UVC
  (`GET_CUR`/`SET_CUR`): set volume/mute through the feature unit,
  select the AS altsetting that carries the negotiated format.
- **v1 = UAC 1.0** (the QEMU model and the compatibility baseline of
  most simple devices); UAC2/3 are later scope (clock entities,
  different descriptor/alt shapes).

## 3. Design

- **Isochronous pipes (shared prerequisite with UVC)**: isoc OUT and
  IN support in the HCD core (XHCI first), a generic isoc-pipe API —
  one milestone serves both the camera and the audio plans.
- **UAC layer**: enumerate AC/AS interfaces; parse terminals, feature
  units, and format descriptors; negotiate (altsetting for the
  format/rate); stream PCM over isoc OUT/IN.
- **House surface**: attach as an OSS dsp-class driver — playback
  (isoc OUT) and capture (isoc IN) behind the same node shape the
  audio family uses; volume/mute via the feature unit behind a
  mixer-style ioctl.
- **Format reality**: PCM is the universal baseline (16-bit
  stereo/48 kHz-class); QEMU usb-audio offers simple PCM — no codec
  dependency, unlike UVC's MJPEG.

## 4. Milestones

### UA0 — Isochronous OUT + IN (shared)
Isoc pipe support both directions on XHCI (from the UVC plan's UV0,
extended to OUT).
**Acceptance**: an isoc OUT stream delivers payload to a QEMU
usb-audio sink without error; isoc IN (the UVC case) still passes;
existing control/bulk/interrupt show zero regression.

### UA1 — UAC1 enumeration
AC/AS interfaces recognized; terminal/feature-unit/format descriptor
parse into a PCM capability table.
**Acceptance**: QEMU usb-audio enumerates with its real rate/format
set; malformed descriptors fail cleanly.

### UA2 — Playback
Negotiate rate/format (altsetting), stream PCM over isoc OUT.
**Acceptance**: guest playback of a known PCM buffer runs error-free
at the negotiated format; the QEMU audio backend receives frames
(host-side wav capture caveats noted — the earlier QEMU zeros issue);
stop/start is clean.

### UA3 — Capture
Isoc IN microphone capture at the negotiated format.
**Acceptance**: a guest capture tool records from the device with
exact frame boundaries; playback + capture coexist.

### UA4 — Volume/mute + hardening
Feature-unit `GET_CUR`/`SET_CUR` behind a mixer ioctl; unplug
mid-stream, negotiation fuzz, isoc error handling.
**Acceptance**: volume/mute set/read round-trip; unplug during
streaming is clean; the fuzz battery never faults; re-plug
re-enumerates.

## 5. Out of scope (recorded)

- UAC2/UAC3 (clock entities, alt shapes) — UAC1 first; MIDIStreaming
  interfaces; host-side mixing of multiple USB devices (which node
  owns `/dev/dsp` when several exist is an open item, matching the
  audio family's single-dsp reality); H.264-style compression (PCM
  only, like the family).
