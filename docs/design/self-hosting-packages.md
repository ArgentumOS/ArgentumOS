# Self-hosting package manifest (FNX)

Status: **REFERENCE — the living list of what FNX must ship to rebuild
kernel + userland + toolchain from source on itself.** Milestone
context: docs/design/self-hosting-plan.md (SH-0..SH-5). Nothing ships
yet; every entry below is a *required* member of the on-FNX build
world. Admission criteria for every package: **permissive license (no
GPL/LGPL on-FNX)**, pinned version, musl+clang-buildable, documented
build step. Host-side tools may differ freely (GPL make/cmake on the
host are fine); the roof constrains what runs on FNX.

## 1. What "ship for self-hosting" means

The running system (and its build volume) must contain everything an
on-FNX rebuild needs, without the host: (A) seed binaries, (B) sources
to rebuild every first-party artifact, (C) build drivers, (D) tooling
that configure/build steps call, (E) the shell/utils baseline. Maps to
the plan's G3 gate and SH-0..SH-5.

## 2. Manifest

### A. Toolchain (SH-0..SH-3)

| Package | Version/pin | License | Notes |
|---|---|---|---|
| LLVM source | llvmorg-19.1.7 (`.build/llvm-src` seed) | Apache-2.0 + MIT (libc++) | the self-hosting seed |
| clang / clang++ | built from the pin | Apache-2.0 | the one compiler (doctrine) |
| lld | same | Apache-2.0 | |
| compiler-rt, libc++, libc++abi, libunwind | same | Apache-2.0/MIT | M2-proven stack |
| llvm-ar / llvm-nm / llvm-objcopy / llvm-ranlib | same | Apache-2.0 | the kernel/userland build tools (GNU binutils replaced) |
| musl source + built libc/loader | current pinned musl | MIT | libc.so + ld-musl (shared world) |

### B. Sources (everything first-party rebuilds from)

- **FNX kernel tree** + `mk/` build fragments.
- **First-party userland**: tools/, tests/, demos/, scripts/, the Xfb
  server tree, (later) Shrike/Kestrel/urxvt.
- **Base userland upstreams**: dash, toybox (submodule + patches).
- **X stack sources**: xorgproto, xcbproto/libxcb, libX11, xtrans,
  libXau/libXdmcp, pixman, libxkbfile, libXfont2/libfontenc,
  fontconfig, **FreeType**, **HarfBuzz** (full-feature build per
  shrike-plan).
- musl and LLVM source as in §A (their own rebuild inputs).

### C. Build drivers

| Driver | License | Role |
|---|---|---|
| **bmake** (BSD make) | BSD-2-Clause | system driver for the tree (GNU make is GPL — out) |
| **ninja** | Apache-2.0 | the executor for the LLVM stage (and any CMake+ninja third party) |
| **CMake** | BSD-3-Clause | the *configure* generator — ships on-FNX (decided): bootstraps with clang++ + bmake, vendored deps, OpenSSL off; host CMake is needed only for the very first cross-seed. In-guest clang source changes reconfigure in-guest — no host round-trip |

### D. Configure/build tooling

- **pkgconf** (ISC) — the pkg-config-compatible tool for third-party
  configure (X stack/FreeType/HarfBuzz); pkg-config (freedesktop) is
  GPL-2+ — out (docs/design/self-hosting-plan.md §4 dev-tooling note).
  Buildable on-FNX via Muon (C) or `pkgconf-lite` (`Makefile.lite`).
- **awk — onetrueawk/awk (picked)** — permissive (Lucent 1997
  attribution notice, no GPL), active, musl-clean; plain C + `-lm`.
  The parser (`awkgram.y`) is regenerated **in-guest by byacc** — no
  host-generated files committed, no bison on-FNX.
- **byacc (Berkeley Yacc)** — **public domain** (verified: "Anyone may
  freely distribute source or binary forms… whether unchanged or
  modified"); the parser generator, plain small C. Covers
  yacc-class grammars (awk's is plain yacc); bison *extensions*
  (glr, api.pure, …) would still need the host — none in the manifest
  set needs them. (GNU bison is GPL — out; NetBSD yacc is a
  BSD-licensed byacc-lineage alternative.)
- **patch — NetBSD `usr.bin/patch` (picked)** — Larry Wall 1986 lineage,
  BSD 2-clause (verified headers); full unified/context/reject/fuzz
  feature set. Needs a ~100-line compat shim on musl (bundle
  `getopt_long`, `__RCSID`/`__dead` shims, `pathnames.h`) and
  **replace/stub `backupfile.c`** (FSF-authored — replace with an own
  permissive implementation of the `-b/-B/-V` backup naming). Not
  OpenBSD's (pledge/unveil baked into `main()`).
- **sed/grep/diff/head/tail/…, an editor** — toybox (sed/grep/diff
  present; toybox vi is the in-guest editor baseline; the future
  first-party Editor app is the long-term home).
- **dash** — the shell for configure/build scripts (already FNX's).
- **tar/cpio/file transfer** — toybox; sources arrive on the build
  volume as tarballs — **no network at runtime** is a requirement.

### E. Volumes/infrastructure (not software, but required)

- A dedicated build volume sized for LLVM source + two build trees
  (≈1 GB+, self-hosting-plan §5); RAM budget for clang/lld link jobs.

## 3. Gap list (picks open or to confirm at adoption)

1. ~~awk~~ **Resolved**: onetrueawk/awk (permissive Lucent notice),
   parser regenerated in-guest with **byacc** (public domain).
2. ~~patch~~ **Resolved**: NetBSD usr.bin/patch (BSD 2-clause) with a
   musl compat shim + own `backupfile.c` replacement.
3. ~~Confirm toybox coverage~~ **Answered**: toybox ships `patch`
   (unified-only), with `diff`/`vi`/`awk` under `toys/pending` (built
   only when enabled); sed/grep present. The dedicated awk/patch picks
   above are the build-tooling path; toybox patch stays as a fallback.
4. ~~cmake avoidance~~ **CMake ships on-FNX (decided)**: BSD-3-Clause,
   bootstrap with clang++ + bmake (vendored deps, OpenSSL off); the
   LLVM stage configures in-guest, so in-guest clang changes need no
   host round-trip. (Muon remains an option only for non-CMake,
   Meson-based packages — none currently needed.)
5. Fonts (Liberation) are GUI content — not self-hosting-critical.

Ordering note: gaps 1–2 (awk/patch) are resolved by the picks above;
they were the first real blockers — they gate in-guest configure of the
third-party sources (SH-2/SH-4), not the clang/musl core (SH-0..SH-3
use CMake-ninja/ninja + plain rules).

## 4. Policy

- Every package: permissive license, pinned version recorded here when
  adopted, musl+clang build recipe documented.
- On-FNX-only rule: the host may use GPL tools (gmake, cmake,
  pkg-config) as build utilities; nothing GPL ships or runs on FNX
  (self-hosting-plan §6 non-goals).
- Version churn: LLVM stays 19.1.7 (self-hosting-plan §4); other pins
  recorded at adoption.

## 5. Non-goals

No package manager (a static manifest + tarballs); no runtime network;
no GPL tool on-FNX (make→bmake, gcc→clang, pkg-config→pkgconf,
gawk→one-true-awk, GNU patch→permissive patch); no source control on
FNX (source delivered as tarballs + patches).

## 6. Keeping this manifest current (standing policy)

**As software is added to FNX, record its self-hosting requirements
here.** Every adoption (a port, a new package, a new third-party
library) must update this document — the manifest is a living list, not
a snapshot:

1. **Admission line**: license (permissive — GPL/LGPL members are
   blocked, name the permissive replacement), pinned version, musl +
   clang build recipe.
2. **New build-time requirements**: any tool the package needs to
   *rebuild itself from source on-FNX* — generators (yacc/lex/gperf/
   ragel), configure machinery (autotools/meson/cmake), scripting
   (python is out — note the replacement), etc. If the tool isn't
   already in §C/§D, that is a **manifest update, not an exception**:
   find the permissive member of the family (bison→byacc,
   make→bmake, pkg-config→pkgconf, gawk→onetrueawk) or record the gap.
3. **Format/content notes**: new fonts, data files, or formats the
   package ships that the self-hosted rebuild must also reproduce.
4. The check is symmetric: a package is only *adopted for the system*
   when its on-FNX rebuild path is complete and recorded.

History of this discipline: CMake (BSD-3) ships so clang reconfigures
in-guest; byacc (public domain) ships so awk's parser regenerates
in-guest; awk = onetrueawk, patch = NetBSD usr.bin/patch — each gap was
closed by finding the permissive member, never by accepting GPL.
