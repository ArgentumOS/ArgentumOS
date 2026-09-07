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
| **ninja** | Apache-2.0 | the LLVM stage; `.ninja` generated host-side by CMake (cmake itself does **not** ship — regenerate on the host when LLVM sources change; long-term option: Muon) |

### D. Configure/build tooling

- **pkgconf** (ISC) — the pkg-config-compatible tool for third-party
  configure (X stack/FreeType/HarfBuzz); pkg-config (freedesktop) is
  GPL-2+ — out (docs/design/self-hosting-plan.md §4 dev-tooling note).
  Buildable on-FNX via Muon (C) or `pkgconf-lite` (`Makefile.lite`).
- **awk** — toybox has none. **GAP**: a permissive awk is required
  (one-true-awk, MIT-style license — the pick to confirm at adoption).
- **patch** — permissive source required (OpenBSD patch, ISC-style;
  confirm toybox `patch` coverage first). **GAP/pick**.
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

1. **awk**: permissive source (one-true-awk recommended). Blocks
   autotools-style configure scripts in-guest (X stack/FreeType/
   HarfBuzz all configure).
2. **patch**: permissive source (OpenBSD patch) unless toybox coverage
   suffices.
3. Confirm toybox coverage of diff/tar/vi/sed/grep for the configure
   toolchain.
4. **cmake avoidance**: LLVM reconfiguration in-guest requires either
   host regeneration (accepted v1), shipping cmake later, or Muon
   (C, permissive) — decide when in-guest LLVM source changes first
   matter.
5. Fonts (Liberation) are GUI content — not self-hosting-critical.

Ordering note: gaps 1–2 are the *first* real blockers — they gate
in-guest configure of the third-party sources (SH-2/SH-4), not the
clang/musl core (SH-0..SH-3 use CMake-ninja/ninja + plain rules).

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
gawk→one-true-awk, GNU patch→permissive patch); no cmake on-FNX
initially; no source control on-FNX (source delivered as tarballs +
patches).
