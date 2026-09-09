# Bluetooth support

Status: **PLAN (2026-09) — scoped as a phased map; no code.** Bluetooth
is not a driver, it is a protocol family: HCI (transport) → L2CAP
(channels) → SDP/GATT (discovery) → profiles (HID, audio, serial, …).
BlueZ (the reference stack) is GPL → out; this is **from-scratch
against the Bluetooth SIG specs**, floor by floor. The largest single
subsystem this project has ever considered; each phase below is its
own substantial milestone. Trigger for starting: a real USB BT dongle
on hardware (no QEMU model — see grounding).

## 1. Grounding (honest)

- BT controllers reach FNX as **USB vendor-class devices** (class
  0xE0) carrying the HCI transport — the USB stack is ready, but the
  HCI transport itself is vendor-flavored HCI-over-USB, a first
  component.
- **No QEMU BT model** (host-bluez passthrough is the only legacy
  vehicle) — every phase verifies on a **real dongle**, the way the
  USB/audio work verified only after QEMU tolerance ran out.
- Classic BT and BLE are two largely separate worlds sharing only the
  lower floors; a plan must not blur them.
- Reference stacks are GPL (BlueZ) → specs only, per house doctrine.

## 2. The floor plan

```
B0 HCI transport (USB) ── command/event/ACL
B1 L2CAP + SDP ───────── channels, service discovery
B2 Classic profiles ──── SPP, HID (to the input seams)
B3 Pairing/security ──── SSP, link keys
B4 LE: GAP + GATT ────── (the separate BLE world)
B5 Audio (A2DP) ──────── far horizon (codec work)
```

### B0 — HCI transport (USB)
Vendor-class enumeration; HCI command/event/ACL over the USB bulk/
interrupt pipes; controller reset, local-version, inquiry.
**Acceptance (real dongle)**: inquiry discovers nearby classic
devices; command/event round-trips; no controller hang across
reset.

### B1 — L2CAP + SDP
L2CAP channels over ACL (connection-oriented + connectionless),
packet reassembly, channel state; SDP queries (service records for
the profiles below).
**Acceptance**: an SDP query against a real peer returns its service
records; channel open/close is clean.

### B2 — Classic profiles: SPP, then HID
**SPP** (serial over L2CAP) first — FNX's dev-board/serial affinity
makes it the honest starter profile and it needs no pairing UI.
Then **HID** (BT keyboards/mice): the events feed the *same* input
seams as the USB-HID plan (consumer keys, pointers) — one event
consumer, two transports.
**Acceptance**: SPP connects to a real BT serial peer (a dev board)
byte-exact; a BT keyboard types into the shell via the shared input
path.

### B3 — Pairing/security
SSP (numeric-comparison/passkey), link-key generation and
**persistence in a config domain** (paired-device records), Secure
Simple Pairing UI hook (a desktop prompt later).
**Acceptance**: a pair/unpair cycle with a real device persists
across controller resets; legacy-pin fallback documented.

### B4 — LE: GAP + GATT
The BLE world: GAP advertising/scan, GATT client (attribute
discovery, read/write/notify) — serves LE peripherals (and later
HID-over-GATT).
**Acceptance**: a real LE peripheral's services/characteristics are
discoverable and readable; notifications arrive.

### B5 — Audio (A2DP) — far horizon
BT speakers/headsets: A2DP/AVRCP streaming + SBC codec. The audio
*consumers* exist (OSS family), but codec work + isoc-like streaming
make this the largest single floor; recorded, not scheduled.
**Acceptance** (whenever attempted): a BT speaker receives PCM
through the OSS surface.

## 3. Relationship

- Input events land in the **same seams as the USB-HID plan** (HID-
  over-BT is a transport, not a new input world).
- Audio lands in the **OSS family** (A2DP is a source, like the
  wired drivers).
- Pairing state is machine data → **config domain** records.
- Explicitly out (recorded): BLE mesh, OBEX/file transfer, PAN,
  SCO voice, vendor codecs — each only if a real consumer appears.

## 4. Honest posture

- The map exists now so the architecture is settled; **the code
  starts on the trigger** (a real dongle in hand for B0's acceptance)
  — the same trigger discipline as the kernel debugger. B0–B2 form
  the defensible first package (transport → channels → SPP+HID);
  B4/B5 are their own later projects and should be read as a map,
  not a commitment.
