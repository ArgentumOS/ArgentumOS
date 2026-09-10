# Audio mixer — one device, many streams

Status: **PLAN (2026-09) — decided in direction; not scheduled.**
Answers the standing gap: the OSS `/dev/dsp` family is direct-device,
so today exactly **one program can play at a time** (`tone.c` opens
`/System/Devices/Audio/dsp`, sets S16_LE/44100/stereo, and writes).
This plan inserts a mixing layer so many apps share the DAC.

## 1. The shape, and what was rejected

A **system-wide audio service (`audiod`)** owns the hardware output
and mixes N client streams in user space — the `coreaudiod` role,
built as a house service. Rejected: **PipeWire** (MIT — license
admits it, but it is a large foreign framework with a D-Bus-shaped
graph model; the house pattern is a small first-party service, as
with pasteboard/keychain) and **PulseAudio** (LGPL — out on
license). The kernel interface stays OSS; the service is the arbiter.

**System-wide, not per-session**: the sound card is a *machine*
resource (unlike the clipboard, which is per-user state), so one
service owns one output device, with **per-session/per-user policy**
layered on top — the Display-principal analogy: the active session's
streams play, others are held/muted.

## 2. Kernel work (the surface is thinner than OSS implies)

Grounding today: the drivers offer only `SNDCTL_DSP_{RESET,SPEED,
STEREO,GETBLKSIZE,SETFMT,CHANNELS}`, accept **only `SNDCTL_DSP_SETFMT
= AFMT_S16_LE`** ("the codec is S16-only"), there is **no `/dev/mixer`**,
no fragment/space ioctls, and no explicit open-exclusivity. So:

- **`/dev/mixer` + OSS mixer ioctls** (`MIXER_READ`/`MIXER_WRITE`,
  `SOUND_MIXER_VOLUME`, per the OSS spec) — hardware gain where the
  hardware has it (AC97 mixer regs, HDA amp widgets, the SB16/GUS
  mixer chips), a clean no-op where it does not. **Software gain
  remains the baseline** (see §3) so behaviour is uniform.
- **Exclusive-open semantics** (a second open → `EBUSY`) so the
  service's ownership is enforceable and no app can steal the device
  behind the mixer's back; plus the deliberate release path for
  direct mode (§3).
- **Buffer sizing / space queries** (`SNDCTL_DSP_SETFRAGMENT`,
  `SNDCTL_DSP_GETOSPACE`) — the right way for the service's periodic
  loop to avoid blocking-write stalls. Audit item: if deferred, the
  service can start with blocking writes and one thread per device.
- **Format**: keeping S16_LE at the device is accepted — conversion
  is the service's job (§3), not a kernel feature.

## 3. The service

- **Mixing core**: N client streams → one output. Per-stream format
  (u8/s16/s24/s32/f32), rate, and channel count are converted to the
  device's S16_LE/rate/channels: format conversion, **resampling**
  (linear interpolation first — quality resamplers are a later
  adoption), channel mapping (mono↔stereo), per-stream **software
  gain**, a clipping policy, and underrun concealment (repeat the
  last buffer rather than glitch).
- **Transport**: AF_UNIX control channel + **MIT-SHM ring buffers**
  for PCM (already DONE, `f0429a0`) — the pasteboard pattern: no
  syscall per buffer, `poll` for wakeups, a per-stream state machine
  (created → running → drained → closed).
- **Client API**: a first-party audio library
  (`argentum::Audio`/libaudio) — open stream (format/rate/channels,
  buffer hint), queue PCM, per-stream volume/mute, pause/resume,
  notify (space available / underrun), close.
- **Volume model, three levels**: per-stream (app-requested) →
  per-session output level (the user's system volume) → master /
  hardware (via `/dev/mixer` where available). Persisted in the
  config corpus (`system.audio` machine scope + per-user overrides) —
  no bespoke config file.
- **Per-app attribution** keyed on the **bundle identity** from the
  launch plan (`bundle` > `verified` > `self-asserted`), so per-app
  volume and policy rest on an identity a shell cannot casually
  spoof; identity-less (CLI) clients fall into the `self-asserted`
  bucket. Honest: this is attribution/UX integrity, not enforcement.
- **Direct (exclusive) mode**: an app may request the raw device
  (latency-critical/game use). The service **releases** `/dev/dsp`;
  that app then opens it itself and **mixing stops by definition** —
  a documented tradeoff, grantable by policy/config, and refused
  while other streams are active unless forced.

## 4. Integration

- **SDL**: the SDL plan's OSS `dsp` backend gives exclusive access;
  the correct default is a **mixer-native SDL audio backend** (a
  small custom SDL audio driver over libaudio) so games mix with
  everything else, with the `dsp` backend kept for direct mode and
  hardware bring-up (SDL plan amended).
- **Desktop system sounds** become clients of the same service (a
  system stream, always present, lowest priority).
- **Driver bring-up is unchanged**: `tone.c` keeps talking to
  `/dev/dsp` directly — the mixer is an application-level layer, and
  direct device testing must stay possible.

## 5. Risks and honest limits

- **No SMP yet (single CPU)**: the mixer competes with the very app
  it is mixing. Design for **generous buffers (target ~50–200 ms
  latency)**, not 5 ms; keep mixing cheap (fixed-point arithmetic,
  cheap resampling first). **Low latency is tied to the SMP/timer
  work** — deferred, stated rather than promised.
- **Timer/sleep granularity**: FNX's sleep granularity is coarse and
  has been *unreliable* in places (recorded in the sb16/gus driver
  notes). The service must tolerate late wakeups — hence underrun
  concealment and drift correction, not tight deadlines.
- **Not a security boundary**: per-app attribution is honest
  reporting, not isolation; a same-user process can claim the
  `self-asserted` bucket, and nothing here is a sandbox.
- **Capture/recording is not in v1**: the drivers are
  playback-focused; which (if any) expose ADC paths (AC97/HDA) is an
  audit item for A5.

## 6. Out of scope (recorded)

Capture/recording (A5, audited), multiple output devices and device
selection beyond config, network/streaming audio, virtual
devices/loopback, resampler-quality upgrades (trigger: audible
artifacts), pro-audio low-latency mode (trigger: SMP + timer work),
and MIDI scheduling (the USB MIDI plan is separate).

## 7. Milestones

### A0 — Kernel surface
`/dev/mixer` + mixer ioctls; exclusive-open (`EBUSY`); buffer/space
ioctls (or the blocking-write fallback documented).
**Acceptance**: `tone.c` still plays; a second open gets `EBUSY`;
master volume changes are audible on hardware-gain cards (AC97/HDA)
and a clean no-op elsewhere.

### A1 — Service core, one stream
Owns the device; one client stream; conversion to S16_LE/device rate;
the write loop. **Acceptance**: a tone/WAV plays end-to-end through
the service on the QEMU audio cards (the established tone-verification
pattern).

### A2 — Mixing
N streams; per-stream gain/mute; linear resampling; channel mapping;
underrun concealment. **Acceptance**: two clients mix simultaneously
with no dropouts; each stream's volume changes independently and is
audible.

### A3 — Policy and persistence
Three-level volume in `system.audio` + per-user overrides; per-app
(bundle-identity) attribution; active-session gating.
**Acceptance**: levels persist across reboot; two apps' levels are
independent; a background session's streams are held/muted.

### A4 — Integration and direct mode
Desktop system sounds; the SDL mixer-native backend; the
exclusive/direct release path. **Acceptance**: two SDL apps mix;
direct mode yields the device (mixing stops, documented) and returns
it cleanly.

### A5 — Deferred
Capture/recording (after the ADC audit), higher-quality resampling,
multiple output devices.
