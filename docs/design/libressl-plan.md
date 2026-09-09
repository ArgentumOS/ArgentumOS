# LibreSSL as the system SSL library

Status: **PLAN (2026-09) — decided in direction; not scheduled.**
LibreSSL (the OpenBSD fork of OpenSSL) becomes **the** system SSL
library: `libssl` + `libcrypto` + the first-party `libtls` simple
API, plus the `openssl(1)` command. One SSL library, period (house
doctrine: a single system crypto library, chosen once).

## 1. Why LibreSSL — and the admission check

- **License**: LibreSSL core is ISC; a few retained legacy files
  carry the OpenSSL license (permissive, no copyleft) — admissible
  under the permissive roof (`docs/design/self-hosting-packages.md`
  admission line: permissive, pinned version, musl+clang-buildable,
  documented build step). BlueZ-style rejection does not apply.
- **Affinity**: the OpenBSD lineage is this project's own reference
  culture; LibreSSL is the "clean OpenSSL" — modern cipher hygiene,
  constant-time work, TLS 1.3 (since 3.6), small attack surface —
  values the OS already claims.
- **Rejected**: OpenSSL itself (Apache-2.0, admissible, but the
  un-forked cruft LibreSSL exists to fix — and one system library
  means we pick the fork with the values); GnuTLS (LGPL — out);
  wolfSSL (GPLv2/commercial dual — out); BearSSL/mbedTLS (permissive
  but minimal profiles without the OpenSSL-compatible API surface
  the ecosystem expects — not a system library).
- **Pin**: LibreSSL **4.3.2** (2026-05-25), the current stable
  branch. Stable branches live ~1yr; the pin regenerates on the
  security-update cadence, not ad hoc.

## 2. Grounding — the kernel surface is already enough

- **Entropy**: `getrandom(2)` (syscall 318, urandom semantics —
  never blocks) is implemented; LibreSSL's `arc4random` prefers
  exactly this backend. No `/dev/urandom` dependency, no kernel
  deltas expected.
- **Threads**: lock hooks bind to musl pthreads (present). **Time**:
  the time64 work is done (LibreSSL needs sane `time_t` only).
  **Sockets**: TLS rides plain TCP — present. **fork/pid**: forking
  is fine (`arc4random` is fork-safe by design).
- **Build route — autotools-free, by rule**: the library's own
  autotools layer (configure.ac / Makefile.am / autogen / the dist
  `configure`) is **never invoked — not on the host, not on-FNX**;
  the pinned tarball is source only. The build is **CMake**, the
  roster's in-guest configure generator (`cmake -S libressl -B build`
  does its own feature detection and config-header generation — no
  Makefile.in execution, no GNU make, no autoreconf anywhere). Host
  and guest use the same CMake recipe, so the on-FNX rebuild is the
  cross-seed's recipe, not a second build system. If L0 finds the
  CMake path itself references a header upstream only generates via
  autotools, that is an L0 finding (checked-in template), never a
  reason to run autogen.
- No TLS consumer exists on the OS today — the plan lands the
  library before its first user (see §5 L2).

## 3. FSH placement

- Shared objects → `/System/Libraries/` (flat; loader search spans
  `System/Libraries` + `Shared/Libraries`): `libssl.so`, `libcrypto.so`,
  `libtls.so` + their versioned realnames. No rpath; consumers add
  NEEDED entries.
- `openssl(1)` → `/System/Tools/` (System-owned, read-only).
- Headers: third-party include trees are consumed at build time from
  the pinned source checkout (cross sysroot copy; no global
  `/usr/include`-style tree — FSH maps `/usr/include` into
  `/System/Source Code/` and that stays a source tree, not an
  install target).
- Configure-time `OPENSSLDIR` (defaults to `/etc/ssl` upstream) is
  pointed at the FNX trust store (§4) so *all* default-path lookups
  are FNX-native.

## 4. The trust story (as load-bearing as the library)

LibreSSL verifies against a CA bundle that does not exist on FNX
yet. First-party design:

- Trust anchors are **System content**: an initial pinned CA set
  ships under `/System/Configuration/` as a certs domain, with
  `system.trust.conf` policy (which of the shipped anchors are
  enabled, plus per-user additions in the user scope — the same
  system→shared→user precedence as every other domain).
- `verify`/`SSL_CERT_FILE` defaults resolve to that store via the
  compiled `OPENSSLDIR`; nothing consults Linux paths.
- What exactly ships initially (a full Mozilla bundle vs. a minimal
  set) is an **open decision for L2** — not a blocker for L0/L1.

## 5. Milestones

### L0 — Cross-seed build (host)
Host-side clang + musl sysroot CMake build of the 4.3.2 pin:
`libcrypto`/`libssl`/`libtls` shared+static + `openssl(1)`; install
to a staging sysroot. **Acceptance**: `openssl version` runs; a
host-side `openssl speed`/self-test passes; no GPL-derived build
output in the artifacts.

### L1 — Stage into FSH + in-guest smoke
Stage `/System/Libraries` + `/System/Tools` into the root image.
**Acceptance (in-guest)**: `openssl(1)` runs on FNX; generate a
self-signed cert and complete an **`openssl s_server` ↔ `s_client`
loopback handshake on FNX** (loopback + TCP are in place) — proves
entropy, sockets, and the full TLS state machine on the OS, no
external network needed.

### L2 — Trust store + first consumer (trigger-gated)
Ship the §4 trust store and wire the defaults. The first *real*
consumer is the trigger, not the milestone date — the natural first
users are the file-download paths (`install` from a URL, or a
first-party `fetch`/updater when one is planned). Until then L0/L1
deliver the library; L2's acceptance is: **a real TLS fetch
(https) verifies a real certificate against the FNX store and
succeeds/fails on trust exactly as configured.**

### L3 — On-FNX self-rebuild
Rebuild LibreSSL **on-FNX** from the pinned source (CMake in-guest,
per the self-hosting roster: bmake/cmake/pkgconf present), then
rebuild consumers against it — the full self-host cycle that proves
the library is a maintainable citizen, not a cross-seeded fossil.

## 6. Relationship & out of scope

- Replaces nothing (no SSL exists today); it is the **first** system
  crypto library and stays the only one.
- Out (recorded): DTLS, FIPS mode, OpenSSL-1.1 API-compat beyond
  what LibreSSL itself carries — each only if a consumer demands it.
