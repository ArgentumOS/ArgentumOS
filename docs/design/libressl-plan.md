# LibreSSL as the system SSL library

Status: **SCHEDULED (2026-09-21) — L0–L2 in flight.** The trigger §5's L2 was waiting for has
arrived: the Foundation's HTTP transport (W7) is **libcurl**
(docs/design/foundation-transport-plan.md), and a **`https://` fetch is its first real consumer** —
which is what turns this plan from "decided in direction" into scheduled work.
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

**L0 LANDED (2026-09-21), WITH THREE FINDINGS — and one of them corrects this milestone's own
acceptance sentence.**

* **IT BUILT, AND THE CLOSURE IS THE SHAPE §2 PREDICTED.** `tools/fetch-libressl.sh` (pin 4.3.2,
  sha256-pinned tarball) + `tools/libressl-build.sh` produce `libcrypto.so.57`, `libssl.so.60`
  **and `libtls.so.33`** — the first-party simple API is in the same pin, as §2 said it must be —
  plus `openssl(1)` and `ocspcheck`. Dependency closure, read out of the artifacts, is exactly
  `libtls → libssl → libcrypto → libc` with **no host library in it**, which is the same check
  libcurl's landing needed and the reason both scripts assert it.
* **THE PIN IS A TARBALL, NOT A SUBMODULE, BECAUSE THE GIT TREE DOES NOT BUILD.** `cmake -S` on
  libressl/portable's `v4.3.2` fails twice: `tls/tls.sym` is missing and `VERSION` cannot be read.
  Both are RELEASE-GENERATED, and regenerating them means `./update.sh` — autotools — which §2
  forbids. The release tarball carries them, so the pin is the tarball and the sha256 is what fixes
  it. **This is what §2 meant by "the pinned tarball is source only"**: the tarball is source, and
  its `configure` is still never run.
* **AND THE ACCEPTANCE SENTENCE IS WRONG FOR A CROSS BUILD — CORRECTED HERE.** L0 said
  "**`openssl version` runs**". It cannot: the binary's interpreter is
  `/System/Libraries/ld-musl-x86_64.so.1` (the FSH path the toolchain bakes in, correctly), which
  does not exist on the build host — measured: `cannot execute: required file not found`. So
  **`openssl version` running is an L1 (in-guest) acceptance, not an L0 one**, and L0's host-side
  acceptance is the artifacts and their closure. The `openssl speed`/self-test clause moves with it.

  Two more findings the build carries in `tools/libressl-build.sh`'s header, each a real correction
  of a first guess: the arch include dir needs **`CMAKE_SYSTEM_PROCESSOR`** (setting
  `CMAKE_SYSTEM_NAME` alone does NOT set it, so `HOST_X86_64` was never defined and the asm include
  dir never added), and the bundled **`nc(1)` is declined by a recorded patch** because its
  `socks.c` calls a `b64_ntop()` the release does not ship — and `ENABLE_NC`, the flag that looks
  right, only controls *installing* it while `BUILD_NC` (unconditional, not an option) gates building
  it.

### L1 — Stage into FSH + in-guest smoke
Stage `/System/Libraries` + `/System/Tools` into the root image.
**Acceptance (in-guest)**: `openssl(1)` runs on FNX; generate a
self-signed cert and complete an **`openssl s_server` ↔ `s_client`
loopback handshake on FNX** (loopback + TCP are in place) — proves
entropy, sockets, and the full TLS state machine on the OS, no
external network needed.

**L1 IS IN FLIGHT (2026-09-21), AND WHAT WORKS IS MEASURED SEPARATELY FROM WHAT DOES NOT.**

* **THE LIBRARY AND THE TOOL RUN ON THE GUEST.** `openssl version` answers `LibreSSL 4.3.2`, and
  `openssl req -x509 -newkey rsa:2048` **generates a key and a self-signed certificate in the guest**
  (exit 0, `subject=CN = localhost`, `cert.pem` + `key.pem` written) — so entropy, the RNG, RSA key
  generation and the X.509 writer all work on FNX.
* **AND IT FOUND THE CONFIG GAP, WHICH IS WHY L1 COULD NOT HAVE LANDED EARLIER THAN THIS.** The first
  run could not make a certificate AT ALL:

      WARNING: can't open config file: etc/ssl/openssl.cnf
      Unable to load config info from etc/ssl/openssl.cnf      <- exit 1, no certificate

  because the compiled-in default `OPENSSLDIR` was the Linux `etc/ssl` — and **this system has no
  `/etc`**. So §4's requirement ("nothing consults Linux paths") was not decoration: it was the thing
  standing between the tool and running. It is fixed — the build passes
  **`-DOPENSSLDIR=/System/Configuration/ssl`**, so the config, the default CA file and the CA directory
  all resolve under the FSH (verified in the artifact: `/System/Configuration/ssl/cert.pem`, `.../certs`,
  `.../openssl.cnf`), and L2 gets its canonical location for free. **THAT FLAG ALSO FORCED A SECOND
  FIX**: CMake installs the config to `CONF_DIR`, which IS `OPENSSLDIR`, so an absolute `OPENSSLDIR` made
  the *host-side* install try to create `/System/Configuration/ssl` ("Maybe need administrative
  privileges", measured) — the install now goes through a `DESTDIR` stage. Both live in
  tools/libressl-build.sh.
* **AND THE HANDSHAKE DOES NOT COMPLETE YET — A DIFFERENT FINDING, AND NOT A TLS ONE ON THE EVIDENCE.**
  The two processes DO reach each other: `s_client` prints `CONNECTED(fd)` and `s_server` logs `ACCEPT`,
  so the TCP **connect and accept on loopback work**. But **no handshake bytes move**: with `-state` on
  the client and stdout/stderr split, the client's state trace is **EMPTY** — it never reaches its first
  state transition — and the server never gets past the accept. What that says is that the place to look
  is the **plain TCP data path**, and it says something worth stating plainly: **a request/response
  payload over loopback has never actually been demonstrated on this system.** A *refused* connect gives
  `ECONNREFUSED` (so routing and the listener work), but a payload has not been carried. **So the next
  diagnostic is not a TLS one: it is a two-process plain-TCP echo over `127.0.0.1`, and it belongs with
  the kernel's network layer.**
* **ONE CHECK WAS PASSING FOR THE WRONG REASON, AND IS FIXED.** `server-completed-a-handshake` grepped
  `ACCEPT` — which `s_server` logs on the TCP **accept** — so it passed on a run where the client hung
  mid-handshake. It is now `server-accepted-the-connection`, named for what that evidence supports.
* **AND TWO FACTS ABOUT WRITING AN IN-GUEST PROBE HERE, EACH OF WHICH COST A RUN.** This FSH has **no
  `/dev/null`**, so a `2>/dev/null` redirect is itself an error the shell reports (redirect to a file in
  the work directory instead); and the guest's toybox has **no `tr`**. Both are corrected in
  `userland/tests/libressl_l1.sh`, which also runs its client under a **bounded wait** so a stalled
  handshake reports instead of hanging the case.
Ship the §4 trust store and wire the defaults. The first *real*
consumer is the trigger, not the milestone date — and **the trigger has
arrived: libcurl, the Foundation's HTTP transport (W7), whose `https://`
fetch is a real TLS consumer** (docs/design/foundation-transport-plan.md —
the user's 2026-09-21 decision brought this plan forward *for* it). Before
that, the named candidates were the file-download paths (`install` from a
URL, or a first-party `fetch`/updater when one is planned). L0/L1 deliver
the library; L2's acceptance is: **a real TLS fetch
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
