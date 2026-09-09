# UDF filesystem support

Status: **PLAN (2026-09) — scoped; no code.** Adds UDF
(ECMA-167 / ISO 13346 / OSTA UDF) to the filesystem roster as the
modern optical format: commercial DVD-Video/data discs and Blu-ray
speak UDF, not ISO9660. Sibling driver of `fs/iso9660`; joins the
optical row (ISO9660 + Rock Ridge today).

## 1. Scope

- **v1: read-only UDF 2.01 / 2.50 data discs** (DVD and BD data +
  video discs whose files are wanted as data). Fixed discs first;
  rewritable (VAT) reads as a milestone.
- **Write / burning is out of scope** (packet + incremental write is a
  separate, larger project; revisit only if a real burning consumer
  appears). Multi-session beyond the first is out in v1. UDF 2.50
  metadata-partition and sparable-partition quirks are recorded, not
  implemented.
- From-scratch driver (house doctrine): ECMA-167 / the OSTA UDF spec
  are the reference; no GPL code is adapted.

## 2. Grounding

- Driver shape: a `fs/udf/` directory registering `"udf"` via
  `register_filesystem` (`fs/filesystems.c`), mirroring `fs/iso9660`'s
  structure and the house `fs_operations` contract.
- Block path: ATAPI reads 2048-byte data sectors (DVD/BD data sectors
  are the same command as CD data), so the existing optical read path
  serves UDF discs with no block-layer change; USB/FAT-era drives read
  UDF-formatted media the same way.
- Probing: UDF has no single magic — it is identified by the **anchor
  volume descriptor pointer** (AVDP) at a fixed sector (256 or, on
  rewritten discs, the -257/-2048 scans), carrying an NSR02/NSR03 tag.
  A cheap probe (read sector 256, check the 16-byte tag) slots into
  the mount-time fstype walk after iso9660 for optical devices.

## 3. Format essentials the driver must handle

- **Anchor + VDS**: AVDP → main volume descriptor sequence → the
  partition descriptor(s), logical volume descriptor, and the file
  set descriptor. Partition types 1 (fixed) and 2 (VAT/sparable) are
  the realistic ones.
- **ICBs**: every file/dir is an information control block — a
  descriptor (file entry, extended file entry, or indirect) wrapped
  in the 16-byte tagged-structure header (tag id, location, CRC).
- **Allocation descriptors**: short (8 B) / long (16 B) / extended
  forms describing extents; reading a file = walking its AD chain
  (including multi-extent files). ICB types beyond the flat file
  entry (embedded/indirect) need handling.
- **Directories**: a dir ICB holds file identifier descriptors (FID:
  38-byte header + name); names are **d-strings** — CS0 or OSTA-
  compressed UTF-16 → UTF-8 (FNX is UTF-8-only; decompression is
  required, not optional). No `..` entries exist in UDF.
- **VAT** (rewritable media): a virtual allocation table ICB maps
  logical block numbers through a virtual partition — reading it is
  the rewritable milestone.

## 4. Milestones

### U0 — Probe + mount
Anchor search (fixed sectors; NSR02/03 tag check), VDS walk,
partition + LV + file-set descriptors, `"udf"` registration in the
probe order.
**Acceptance**: a UDF fixture (host-built via `mkudffs`/`genisoimage
-udf`, or a small `tools/mkudf.py` if neither is available) mounts;
`mount -t udf` reports the volume; a bogus disc fails cleanly
(EINVAL/ENOTSUPP), never a panic.

### U1 — File reads
File-entry ICBs, short/long AD extent chains, multi-extent and large
(> one extent run, > 4 GB-class) files.
**Acceptance**: files read byte-identical to the host's copy of the
same disc (cmp against the fixture source); a deliberately truncated
AD chain fails cleanly.

### U2 — Directories + names
Dir ICB → FID walk; OSTA-compressed UTF-16 and CS0 names → UTF-8;
nested trees; partition type 1.
**Acceptance**: a deep fixture tree lists and reads with exact
(UTF-8) names; long names (> 64) and embedded spaces work; stat
matches the host (size/mode/mtime class).

### U3 — Rewritable (VAT) reads
Partition type 2: the VAT ICB → virtual partition mapping.
**Acceptance**: a UDF-rewritten (packet-written) fixture reads
correctly, including files written after the initial format; the VAT
ICB itself is validated before use.

### U4 — Hardening
Tag CRC/location validation, descriptor length and AD bounds checks,
fuzzed-image battery (truncations, flipped bytes at anchor/VDS/ICB
locations).
**Acceptance**: the malformed-fixture battery returns clean errors
with no kernel fault; read-only mount never writes to the device
(verify by checksum before/after).

## 5. Out of scope (recorded)

- Write/burning (packet, incremental, MRW); multi-session links;
  UDF 2.50 metadata partitions; BD-specific MV descriptors beyond
  the VAT class; playback-oriented structures (only file data is
  wanted). Each is a later project if a consumer appears — reading
  data off optical media is the need this serves.
