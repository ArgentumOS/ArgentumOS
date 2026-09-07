# Self-hosting plan (long-term)

Status: **PLAN — long-term vision.** Nothing below is scheduled. This is
the milestone home for the end state that today's toolchain and linking
plans are heading toward: **FNX eventually builds itself — kernel,
userland, and toolchain — with a compiler that runs on FNX.**

Related plans (prerequisite work lives there, not here):

- `docs/design/llvm-clang-toolchain-plan.md` — the **Clang migration** (M1..M5):
  every guest-targeted artifact and the kernel move from gcc to host
  clang/lld. Its Non-goals §7 already says self-hosting is "a later
  milestone (the pinned 19.1.7 source in `.build/llvm-src` is the future
  seed)" — this document is that later milestone, expanded.
- `docs/design/cpp-toolchain-plan.md` §1 — the toolchain-direction note for the
  record: LLVM/Clang for the whole OS, musl the C library, wrapper swap
  `gcc/g++` → `clang/clang++` with the same `--target=x86_64-linux-musl`
  sysroot.
- `docs/design/shared-libraries-plan.md` — **dynamic linking** (decided): shared
  userland libraries, static recovery set kept.
- `docs/reference/os-profile.md` + `4229be1` — the doctrine: one system compiler =
  **Clang**; languages open per subsystem (C, C++, ObjC).

---

## 1. What "self-hosting" means for FNX

End state, in increasing strictness (all three are goals):

1. **On-FNX compiler**: clang/lld/compiler-rt/libc++/libunwind binaries
   that run natively on FNX (built by the migration's host-clang chain,
   then by themselves).
2. **On-FNX userland build**: the FNX userland — musl, dash/toybox set,
   the X stack, the Momo toolkit — is rebuilt from source *on FNX* by the
   on-FNX compiler.
3. **On-FNX kernel build**: the kernel itself is compiled and linked on
   FNX (CC64/kernel64 under guest clang + lld), producing the same boot
   image the host produces.

Classic certification: **the compiler rebuilds itself twice on FNX**
(stage1 → stage2 → stage3; stage2 == stage3 or byte-identical output is
the self-hosting proof), and the on-FNX-built kernel boots.

What legitimately stays host-side: **QEMU** (the execution/test host),
source distribution (tarballs + patches), and the *bootstrap seed* —
the first guest-clang is a host product; nothing after stage1 needs the
host toolchain. What is *not* part of self-hosting: no host compiler is
invoked for guest artifacts once stage1 exists.

## 2. Preconditions (gates owned by other plans)

These are *done* before SH-1 can start. They are listed so the milestone
spine reads correctly, not duplicated here.

- **G1 — Clang migration complete** (llvm-clang-toolchain-plan): kernel
  + userland built by clang/lld on the host; no gcc/g++/gnu-specs
  anywhere in the system build; musl itself is a clang product. The
  wrapper swap already anticipates self-hosting: same
  `--target=x86_64-linux-musl` sysroot, same runtime libs.
- **G2 — Dynamic linking in place** (shared-libraries-plan): shared
  libraries + a working loader on FNX, because a dynamic compiler is far
  smaller to build than a static one and the guest RAM/disk budget is
  tight; the static recovery set stays for the bootstrap-deadlock case.
- **G3 — Build prerequisites on FNX**: the package manifest in
  `docs/design/self-hosting-packages.md` (build driver per §4, plus
  tooling: patch/sed/awk/editor — note toybox has **no awk**, a gap), and
  enough persistent disk for the LLVM source tree (multi-hundred-MB;
  likely a dedicated build volume, not the root image).
- **G4 — RAM/disk budget sized**: clang/LLVM builds are memory-hungry.
  Decide the guest ceiling (see §5) before SH-2; a stage-2 clang build
  needs more than the current default test RAM.

## 3. Milestones

Each milestone is independently verifiable on FNX in QEMU. Timings are
deliberately absent — TCG emulation makes full LLVM rebuilds slow; §5
covers the realism problem.

### SH-0 — Guest clang runs (cross seed)

Host-clang-built clang + lld binaries (static, or dynamic per G2) are
installed on FNX; `clang --version`, `clang -c`, `ld.lld` all run under
FNX. Uses the existing pinned source seed.

**Acceptance:** an on-FNX clang compiles a hello-world C file *and* the
result executes on FNX — the first guest-built guest binary. No host
toolchain was involved in producing that binary.

### SH-1 — Guest clang builds musl

The C library is the bootstrap root (musl is already a clang product per
G1, so this is a clean rebuild, not a compiler switch). Build musl on FNX
with the SH-0 clang into a fresh sysroot.

**Acceptance:** a binary linked against the on-FNX-built musl runs; the
guest musl's `configure` `CC=clang` step completes without host help.

### SH-2 — Runtime libraries on FNX

compiler-rt, libc++, libc++abi, libunwind rebuilt on FNX against the
SH-1 sysroot.

**Acceptance:** the `cpp_smoke` battery (docs/design/cpp-toolchain-plan.md P3)
passes when compiled by on-FNX clang against on-FNX runtimes.

### SH-3 — Clang rebuilds itself (stage1 → stage2)

Full clang/lld rebuild on FNX, using the SH-2 sysroot and the SH-0
compiler as stage1. Then rebuild again with the stage2 compiler.

**Acceptance:** stage2 builds and runs; stage3 (rebuilt by stage2)
produces the same binaries as stage2 — the two-rebuild self-hosting
certification. The stage2 toolchain is now the canonical one and SH-0's
seed is retired to history.

### SH-4 — Userland rebuilds itself on FNX

The whole first-party userland (dash/toybox set, Xfb + X stack, Momo,
system tools, FSH config domains) rebuilt on FNX with the SH-3
toolchain, dynamic linking per G2.

**Acceptance:** the on-FNX-built userland image boots the interactive
Xfb desktop; a documented subset of the guest test battery passes
against guest-built binaries.

### SH-5 — Kernel builds on FNX

kernel64 (and the kernel proper) compiled and linked on FNX by the SH-3
clang/lld — the migration's M3/M4 recipe, run in-guest.

**Acceptance:** the on-FNX-built kernel boots to the same desktop, and
its image is reproducible: rebuilding the same source with the same
toolchain yields an identical image (see §5 on reproducibility).

## 4. Cross-cutting open decisions

- **Build driver**: GNU make is GPL, so the system driver must be
  permissive. Shortlist:
  - **BSD make (`bmake`)** — BSD-2-Clause, small C, trivially
    self-hostable, POSIX make + much GNU coverage. Not a byte drop-in:
    the tree's Makefiles use GNU-isms (`$(shell ...)`, `:=` semantics,
    pattern-rule edges) that need edits or a POSIX-make hygiene pass
    (write the tree portable-POSIX so host GNU make and guest bmake both
    drive it).
  - **Ninja** — Apache-2.0, one small C++ binary, very portable. Reads
    generated `.ninja` files, not makefiles — and it is what clang's own
    build uses (`CMake + Ninja`). CMake (BSD-3-Clause) is a big C++ port
    itself, but it can generate `.ninja` **on the host once**; the guest
    then only needs ninja to rebuild clang, re-shipping the generated
    files when sources change. Python/meson/SCons/redo-family and
    JVM-based Bazel are all out (Python on FNX, Lua, or JVM
    prerequisites that don't exist).
  - **Custom FNX driver** — a small dependency-graph builder in C as a
    system component; fits the educational mission and the permissive
    roof, costs engineering, one-off syntax.

  Two credible routes (not mutually exclusive): **bmake as the system
  driver** for the tree (host dev keeps GNU make; POSIX hygiene makes
  both work), plus **ninja for the clang bootstrap specifically**
  (host-CMake-generated `.ninja`, guest runs ninja). Decide at
  execution; bmake is the recommended default for the tree, ninja for
  the LLVM stage. Affects SH-4/SH-5 acceptance.
- **Kernel-on-FNX timing**: SH-5 is last in the spine, but the kernel
  clang build (G1) may make kernel-in-guest practical earlier — the
  milestone order is not a commitment.
- **Dev-tooling licenses (note)**: pkg-config (freedesktop) is
  **GPL-2+** — not admissible on-FNX. **pkgconf is ISC** (verified
  from its COPYING, 2026) and is the permissive pkg-config-compatible
  tool if any in-guest configure/dev tooling needs one (e.g., third
  parties built against the X stack). pkgconf builds with Meson
  (Python — out on FNX) or **Muon** (a C implementation of the Meson
  build language, for bootstrap environments without Python), and has
  a `pkgconf-lite` single-binary build (`Makefile.lite`). Host-side
  dev tooling is unaffected (the host may use GPL pkg-config; the
  roof constrains what ships/runs on FNX).
- **Reproducibility standard**: byte-identical images are the strong
  target; if the tree can't reach it (paths, build-id), define what
  "reproducible" certifies instead (boots + same behavior + diffable
  binary deltas).
- **Version policy**: stay on LLVM 19.1.7 (llvm-clang plan §7); the seed
  source and the host packages match. A bump only if a language feature
  forces it — and a bump is now an in-guest rebuild, not a host op.
- **Source delivery**: how the source tree + patches reach the guest
  build volume (packaged tarball in `/Shared`, an auxiliary XBFS volume,
  or the root image grows). Ties to disk budget (G4).
- **Testing under acceleration**: full LLVM rebuilds under TCG are
  slow enough to threaten milestone realism (§5).

## 5. Realism notes (not blockers, but plan honestly)

- **TCG speed**: a stage-2 clang rebuild is hours under pure emulation.
  Milestone acceptance should separate *functional* proofs (compile a
  kernel TU, run cpp_smoke) from *full* rebuilds, which may run
  overnight or need KVM/accel in the dev harness (host-only; not a
  product property).
- **RAM**: clang/lld link jobs want >1 GB. Decide the guest memory
  ceiling and whether the kernel's physical-memory support grows to
  match (the address space is already 128 TB; this is RAM sizing, not
  VA). Swap is not on the table (no paging plan) — budget RSS instead.
- **Disk**: LLVM source + two build trees ≈ 1 GB+; plan a dedicated
  build volume (XBFS) rather than growing the root image.
- **Bootstrap deadlock**: the static recovery set (G2) is the escape
  hatch if a dynamic guest toolchain breaks — a static clang fallback
  must always be producible from the last-good seed.

## 6. Non-goals

- Compiling the *build host's* tools on FNX (QEMU itself, host git, the
  image builders) — those stay host-side forever.
- Replacing the migration's host-clang during G1..G3 — the host builds
  the guest toolchain until SH-3 retires the seed.
- Paging/swap support as a self-hosting prerequisite (RAM is budgeted
  within physical memory).
- Any schedule. This document is the map; individual milestones get
  their own execution plans when they are pulled in.
