# LLVM/Clang toolchain plan — one compiler for kernel + userland

Status: **PLAN (2026-09) — design decided, nothing implemented.** The
doctrine is settled (docs/os-profile.md:34-38): one system compiler
toolchain — Clang — builds the kernel, the userland, and every
first-party language; no foreign compilers in the system build. This is
the sanctioned "Clang migration" that docs/cpp-toolchain-plan.md §1
deferred to ("when self-hosting arrives, the wrapper swap is
`gcc`/`g++` → `clang`/`clang++` with the same ABI choices"). It also
unblocks Objective-C (GNUstep evaluation, docs/gnustep-evaluation.md)
as a toolkit language, which needs clang.

## 0. Goal

Replace every GCC invocation in the FNX **system build** with LLVM/Clang
toolchain commands, end state:

- **Kernel** (kernel/, kernel64/, the `.S` files, the EFI link) built by
  clang and linked with lld + llvm-objcopy.
- **musl** (`.build/musl64`) built by clang — the C library is part of
  the system build, so it cannot be a GCC product.
- **Userland** (dynamic default + the static recovery set + the X
  stack) built through clang-based wrapper scripts.
- **C++** (libc++, libc++abi, libunwind, compiler-rt) built by
  clang/clang++ — one exception/unwinding implementation end to end.
- **No gcc/g++/gnu specs anywhere in the build**: `tools/musl-gcc*.sh`,
  the `musl-gcc.specs` mechanism, GNU `ld`/`objcopy` for the kernel.

"System build" = every command that produces guest-targeted binaries
(and the kernel). The *build host* runs clang/lld — host tools are not
guest artifacts. Self-hosting (compiling the compiler under FNX) is out
of scope here (Non-goals, §8).

## 1. Why (from the record)

- One permissive roof: clang, lld, compiler-rt, libc++{,abi}, libunwind
  are Apache-2.0-with-LLVM-exception / MIT — no GPL anywhere even if
  vendored, unlike libgcc/libstdc++ (docs/cpp-toolchain-plan.md:28-34).
- One unwind/ABI story kernel↔userland (LLVM libunwind end-to-end);
  today the kernel is GCC and the userland C++ is GCC-front-ended LLVM
  runtimes — a GCC↔LLVM unwind-integration hazard the plan removes.
- One exception implementation removes the crtbegin/crtend and
  libgcc.a contract the current gcc specs depend on.
- Languages open per subsystem (C, C++, Objective-C) — clang is the
  only front end that speaks all three from one driver.

## 2. Current state (verified 2026-09)

- **Host toolchain**: Debian clang-19 19.1.7 + lld-19 + llvm-19 packages
  installed — `/usr/lib/llvm-19/bin` holds `clang`, `clang++`,
  `ld.lld`, `llvm-objcopy`, `llvm-ar`, `llvm-ranlib`, `llvm-nm`,
  `llvm-strip`, `llvm-objdump` — version-identical to the pinned
  `llvmorg-19.1.7` source in `.build/llvm-src` (tools/fetch-llvm.sh:19).
  The tools are **not on PATH** — wrappers/Makefile must use the full
  path or prepend it.
- **Userland wrappers (all `exec gcc`, gcc front ends today)**:
  - `tools/musl-gcc64.sh`: `gcc -no-pie -I tools/kernel-headers
    -specs $MUSL/lib/musl-gcc.specs` (dynamic ET_EXEC default, M1).
  - `tools/musl-gcc64-static.sh`: same + `-static` (recovery set).
  - `tools/musl-gcc.sh`: i386 legacy, unused by the 64-bit flow.
  - `tools/musl-g++64.sh`: `exec g++ -static ... -nostdinc++ -isystem
    $LLVM_CXX/include/c++/v1 -nostdlib++ ... -lc++ -lc++abi -lunwind`
    (deliberately static + g++ today).
- **The specs contract** (`.build/musl64/lib/musl-gcc.specs`, generated
  by musl's install from `--syslibdir=/System/Libraries`): cpp
  `-nostdinc -isystem $MUSL/include`; `*startfile`: `Scrt1.o crti.o
  crtbeginS.o`; `*endfile`: `crtendS.o crtn.o`; `*link`:
  `-dynamic-linker /System/Libraries/ld-musl-x86_64.so.1 -nostdlib
  %{shared:-shared} %{static:-static}`; `*libgcc`: `libgcc.a`. The
  startfile/endfile crtbeginS/crtendS and libgcc.a are **GCC
  artifacts** — a clang driver cannot consume the specs file and must
  not consume the GCC crt.
- **musl64** (Makefile:181-194): `CC="gcc" ./configure --target=x86_64
  --prefix=.build/musl64 --syslibdir=/System/Libraries`, make/install,
  hardlink `libc.so → ld-musl-x86_64.so.1`.
- **C++ runtimes** (Makefile `llvm-cxx`, 200-227): built from the
  pinned source with `-DCMAKE_C_COMPILER=gcc
  -DCMAKE_CXX_COMPILER=g++` + the specs flags (`-static`), all
  `*_ENABLE_SHARED=OFF`; `.build/llvm-cxx-prefix` = static
  libc++/libc++abi/libunwind only. No compiler-rt anywhere yet.
- **Kernel**:
  - Compile flags (CC64K, Makefile:506-510; CC64R = CC64K +
    `-fvisibility=hidden -MMD -MP`, 546): `gcc -m64 -march=x86-64
    -std=c89 -O2 -fPIC -fno-semantic-interposition -fno-common
    -ffreestanding -mno-red-zone -mno-sse -mno-sse2
    -fno-asynchronous-unwind-tables -fno-stack-protector
    -fvisibility=hidden -DCONFIG_FS_MINIX`. No `-mcmodel` anywhere.
    Every flag is clang-acceptable (`-fno-semantic-interposition` is
    supported since clang 13).
  - Inline asm is standard AT&T extended asm with numbered operands
    (`kernel64/asm64.c`, `idt64.c` incl. a top-level `__asm__` block of
    `.macro ISR_NOERR/ISR_ERR` + 65 expansions, `paging64.c`, `mm64.c`,
    `efi_stub.c` incl. an RIP-read idiom) — clang's integrated assembler
    handles `.altmacro`/`.macro`/`.type @function`; the two `.S` files
    (`kernel64/switch64.S`, `init_trampoline64.S`) are plain AT&T gas.
  - **PATCH_PIC is the GCC-specific hazard**: `tools/patch_pic_data.py`
    byte-patches gcc-14 objects (`mov sym(%rip),%rax` 0x8b → `lea` 0x8d
    for R_X86_64_PC32 relocs to hidden extern data; Makefile:561/574,
    rationale in docs/port-longmode-uefi.txt:286). Clang emits `lea`
    directly for that pattern, so the patch must be **gated to GCC-built
    objects or retired** — an unconditional byte patch over clang
    objects could corrupt unrelated `0x8b` opcodes.
  - Kernel link (Makefile:570-579): `python3 tools/patch_pic_data.py`
    then `$(LD) -m i386pep --entry efi_main --image-base 0x1000000`
    (GNU ld) then `objcopy --remove-section .comment --subsystem 10`
    (GNU objcopy).
- **Stale doc claims to correct during/after the migration**:
  cpp-toolchain-plan.md:50-60 ("gcc -static", "no clang on the host",
  "no dynamic linker, everything -static"), musl-g++64.sh:4-8
  ("deliberately STATIC"), musl-gcc64.sh:1-3 (gcc wrapper header).

## 3. The clang driver contract (replacing the specs)

The wrapper is where the GCC→clang swap happens. `tools/musl-clang64.sh`
(mirroring musl-gcc64.sh) must spell out, per invocation:

- driver: `/usr/lib/llvm-19/bin/clang -no-pie` (keep the fixed-base
  ET_EXEC model the kernel loader implements — PIE mains are rejected).
- headers: `-nostdinc -isystem $MUSL/include` (specs `*cpp_options`)
  and `-I tools/kernel-headers` (the FNX Linux-uapi subset).
- link (specs `*link`): `-nostdlib` + `-Wl,-dynamic-linker,
  /System/Libraries/ld-musl-x86_64.so.1`, no libgcc.
- start/end files: musl's `Scrt1.o crti.o crtn.o` (install-provided)
  + **compiler-rt's crtbegin.o/crtend.o** (NOT gcc's crtbeginS/
  crtendS). A static variant (`musl-clang64-static.sh`) adds `-static`
  and uses musl's `crt1.o`.
- builtins: link **compiler-rt builtins built against musl**
  (`libclang_rt.builtins-x86_64.a`) in place of `libgcc.a` — the specs
  only pulled libgcc for `__udivmodti4`-class helpers; a musl-targeted
  compiler-rt build from `.build/llvm-src` (compiler-rt is part of the
  pinned llvm-project) is the doctrine-clean source.
- The `-m64 -march=x86-64` and `-std=c89 -Wall -Wstrict-prototypes`
  defaults stay where they are today (kernel) / are unneeded (host
  clang defaults to x86-64 LP64 for the userland).

Decision: wrapper-composed flags (not `--sysroot` with a rebuilt
sysroot layout): `.build/musl64` is a flat prefix (include/ + lib/),
not a `--sysroot` tree, and the specs content is fully expressible as
driver args. This keeps the migration a drop-in wrapper swap, exactly as
cpp-toolchain-plan.md §1 promised.

## 4. Milestones

Sequencing rationale: userland first (it has the regression gates and
mirrors how dynamic linking landed: one binary → whole world → remove
the old wrapper), then musl + the C++ runtimes (the ABI decisions are
identical, so no redo), then the kernel (isolated, riskiest), then the
gcc removal gate. Each milestone keeps the gcc path working until its
flip lands.

### M0 — clang userland smoke (the wrapper + builtins)

- Build compiler-rt builtins for x86_64/musl (static, from
  `.build/llvm-src` — one small cmake configure in the llvm-cxx style).
- Add `tools/musl-clang64.sh` + `tools/musl-clang64-static.sh` (§3),
  Makefile `MUSL64_CLANG` vars.
- Build a static hello and a dynamic hello with them; boot both
  (extend .build/m0-style boot: interp `/System/Libraries/ld-musl...`
  on the dynamic one, none on the static).

Acceptance: both binaries boot and print; `readelf -d`/`-l` show the
FSH interpreter on the dynamic hello and no interpreter on the static
one; fshlint 0 on both.

### M1 — the whole userland via clang

- Point the build at the clang wrappers: `MUSL64_CC`,
  `MUSL64_CC_STATIC` (Makefile:147-173), which cascade into dash64
  (229-235), the recovery dash/toybox (258-269), mktoybox.sh
  (TOYBOX_CC), the ~20 userland64 one-shot compiles (366-378, 456),
  FNXLIB libconfig (159-163), the x11 demo bins, and the X stack via
  `tools/x11-shared-build.sh:30` (autotools/meson CC + the cross file).
  Xfb/xkbcomp/xdraw/xkey rebuild through the clang wrapper.
- Keep the gcc wrappers on disk until M4.

Acceptance: **m1_boot green, m2_xfbdesk green, m4_recovery.py all
modes green, fshlint 0** — with every staged ELF built by clang
(verify a couple by `readelf --string-dump=.comment` or build log).

### M2 — musl and the C++ runtimes by clang

- `musl64` target (Makefile:181-194): `./configure CC=clang...` —
  musl's build is self-contained (its own include/), so configure needs
  the §3 driver minus the crt/libgcc pieces; exact CC string worked out
  here (musl-cross-make is the known-good precedent for clang-built
  musl). The generated `musl-gcc.specs` becomes moot (nothing consumes
  it after M1); `--syslibdir=/System/Libraries` still bakes the FSH
  interpreter path into libc.so/ld-musl.
- Rebuild libc++, libc++abi, libunwind with `clang++` + the clang
  wrapper flags (same cmake recipe as llvm-cxx, compilers swapped).
- `tools/musl-g++64.sh` → `tools/musl-clang++64.sh` (`clang++` +
  compiler-rt builtins + the LLVM runtimes; keep `-static` for now —
  the C++ carve-out is static by decision until a C++ shared milestone).

Acceptance: **cpp_smoke green**; m1_boot green on the clang-built musl
(whole world now: clang kernel-excluded userland on clang libc); the
recovery static set still boots (m4 modes).

### M3 — the kernel via clang + lld

- Compile kernel sources with clang using the CC64R flag set verbatim
  (`-std=c89 -fPIC -ffreestanding -mno-red-zone -mno-sse -mno-sse2
  -fno-asynchronous-unwind-tables -fno-stack-protector
  -fno-semantic-interposition -fvisibility=hidden -O2`) — kernel64/*.S
  through clang's integrated assembler.
- **PATCH_PIC gate**: `tools/patch_pic_data.py` runs only over objects
  that still need the gcc-14 mov→lea rewrite; once every kernel object
  is clang, drop the step (clang emits `lea` for the hidden-data
  RIP-relative pattern natively). Verify at the first clang kernel boot
  that no `0x8b` mis-patch occurs in the interim.
- Link: port the EFI link (Makefile:575-578) from GNU
  `ld -m i386pep --entry efi_main --image-base 0x1000000` + GNU
  `objcopy --remove-section .comment --subsystem 10` to **lld /
  llvm-objcopy** (lld-link PE/COFF driver for a UEFI application or
  `ld.lld -m i386pep` if the emulation maps; `llvm-objcopy --subsystem
  10` exists for PE). Exact flags landed here; keep GNU ld/objcopy as
  the fallback until the lld image boots.

Acceptance: **full boot with a clang-built kernel** — m1_boot green,
m4_recovery normal + corrupt modes green (the recovery shell boots from
a clang kernel), serial console + the X desktop on the clang kernel
(m2_xfbdesk).

### M4 — language enablement + the gcc removal gate

- Remove `tools/musl-gcc*.sh`, `musl-g++64.sh`, the `-specs` flag and
  any `gcc`/`g++` reference from the build; add a Makefile/fshlint-style
  **"no GCC in the system build" gate** (grep the build files for
  `gcc|g++|specs`; zero-allow, host-tool docs excepted).
- The LLVM pin may move past 19.1.7 if a language feature needs it
  (libc++ ≥ 20 needs clang — the original reason the pin stayed 19 with
  the gcc fallback; that constraint is gone once M2 lands).
- Update the stale docs (§2 list) + README toolchain wording.
- Objective-C/GNUstep evaluation can then be re-run under clang (its
  own plan — clang is a prerequisite, now met).

Acceptance: full regression matrix green (m1, m2, m4-recovery,
cpp_smoke); `grep -rn "gcc\|g++\|specs" Makefile tools/` zero (minus
explicitly-listed host-tool/doc mentions).

## 5. Risks & gotchas

- **PATCH_PIC** (Makefile:561/574): unconditional byte-patching of
  clang objects is unsafe; the gate must be provenance-based. Highest-
  risk single item in the kernel milestone.
- **EFI link recipe**: GNU ld's `-m i386pep` emulation is not lld's
  native PE path; the UEFI image layout (entry `efi_main`, image base,
  `--subsystem 10`, `.comment` strip) must be re-derived for
  lld/llvm-objcopy and validated by actually booting the image — an
  lld image that "links" but doesn't boot is the failure mode to watch.
- **crtbegin/crtend and libgcc are gcc artifacts**: forgetting them in
  the clang wrapper (or pulling the gcc ones) reintroduces a foreign
  compiler dependency or silent init-array/unwind breakage.
- **fshlint R2/R3**: clang embeds build paths in `.comment`/debug
  similarly to gcc — the kernel already strips `.comment`; userland
  builds have never needed `-ffile-prefix-map` (0 buildpath warnings
  today), so keep an eye out if clang's object layout changes that.
- **The -O2 history**: FNX has hit gcc-`-O2` miscompiles before
  (bounds-check class). Treat clang `-O2` as unproven until the
  stress/regression harnesses pass at each milestone — do not assume
  clean.
- **autotools/meson configure probes** (dash, toybox, x11):
  configure-time compile tests run through the wrapper; clang must pass
  them with the same probe results (mostly `-no-pie` + the musl include
  paths). x11-shared-build.sh already runs cross (`--host`) so dynamic
  test binaries never execute — unchanged.
- **musl's own configure** (M2) is the first clang compile that runs
  before musl exists; its probes must not touch host glibc headers.
  Precedent: musl-cross-make builds musl with a bare cross clang.

## 6. Open items (decided here, refined at execution)

- Exact `ld.lld`/`lld-link` + `llvm-objcopy` flags for the UEFI image
  (M3, validated by boot).
- The precise musl-configure CC string for M2.
- Whether the kernel objects end up needing any compiler-rt builtins
  (they never needed libgcc under gcc — expect none; verify at M3).

## 7. Non-goals

- **Self-hosting**: compiling clang/lld under FNX is a later milestone
  (the pinned 19.1.7 source in `.build/llvm-src` is the future seed);
  the host clang-19.1.7 is the build compiler for now. The long-term
  milestone spine (SH-0..SH-5, from cross-seed to kernel-on-FNX) lives
  in docs/self-hosting-plan.md.
- **LTO**: not required for the switch; thin-LTO through lld is a
  follow-on if the kernel/userland ever wants it (needs llvm-ar for
  thin archives — present at /usr/lib/llvm-19/bin).
- **Version churn**: stay on LLVM 19.1.7 (matches the pin and the host
  packages) unless a language feature forces a bump (M4 note).
- **GNU binutils in the final build**: replaced by lld/llvm-objcopy/
  llvm-ar at M3/M4 per the doctrine's permissive roof; GNU tools remain
  acceptable only as uncommitted host utilities during the transition.

## 8. Status

### M0 — DONE (commit 2678129)

- **compiler-rt builtins**: standalone cmake of
  `.build/llvm-src/compiler-rt/lib/builtins` with
  `COMPILER_RT_DEFAULT_TARGET_TRIPLE=x86_64-unknown-linux-musl`, clang
  19 / llvm-ar / llvm-ranlib, `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY`
  → `.build/compiler-rt/lib/linux/libclang_rt.builtins-x86_64.a`.
  Builtins are freestanding, so no musl sysroot is needed at configure
  time; a 128-bit-divide smoke link confirmed `__udivti3` etc. resolve
  from the archive. The standalone builtins build emits no
  crtbegin/crtend objects — plain C needs none (musl runs .init_array
  itself); the C++ milestone (M2) pulls compiler-rt's crtbegin/crtend
  via the full-runtimes build.
- **Wrappers** (`tools/musl-clang64.sh`, `tools/musl-clang64-static.sh`):
  `clang -no-pie -nostdinc -isystem musl/include -I tools/kernel-headers
  -nostdlib` + musl start files (`Scrt1.o`/`crt1.o` + `crti.o`, `crtn.o`
  last) + `-Wl,-dynamic-linker,/System/Libraries/ld-musl-x86_64.so.1`
  (dynamic only) + `-lc` + the compiler-rt builtins archive. clang adds
  its own resource headers (stddef.h…) even under `-nostdinc`, so no
  extra `-isystem` is needed. The static wrapper omits `-no-pie`
  (`-static` already forces the fixed ET_EXEC model).
- **Makefile**: `MUSL64_CLANG(_STATIC)` vars, `compiler-rt` target
  (cmake configure+build), `m0clang` target staging
  `userland/hello.c` twice under `System/Shared/tests`
  (`clang-hello-dl`, `clang-hello-static`; lint carve-out tree).
  `rootxbfs` chains `m0clang` — `userland64` starts with
  `rm -rf $(ROOTFS64)`, so the clang outputs must be (re)staged after
  it or they never reach the packed image.
- **Acceptance**: both binaries boot and print under FNX
  (`M0_CLANG_HELLOS: True`; pids 8/10); readelf shows the FSH
  interpreter on the dynamic hello (NEEDED libc.so only) and none on
  the static one (0 INTERP, 0 NEEDED, no GLIBC symbol refs); the static
  hello also runs on the build host (musl static) — a full
  clang+compiler-rt end-to-end check; fshlint 0.

Next: **M1** — point `MUSL64_CC`/`MUSL64_CC_STATIC` at the clang
wrappers; the gcc wrappers stay on disk until M4.

### M1 — DONE (commit 6123116)

- **The flip**: `MUSL64_CC` / `MUSL64_CC_STATIC` now point at
  `tools/musl-clang64.sh` / `tools/musl-clang64-static.sh`; every
  hard-coded `tools/musl-gcc64*.sh` recipe reference (dash64, toybox64,
  the recovery dash/toybox, xfb64) was replaced with the vars, and
  `tools/x11-shared-build.sh` exports `CC=musl-clang64.sh` while the
  meson cross file names it for `c`/`cpp`. `tools/mktoybox.sh`'s
  fallback CC is the clang wrapper too. The gcc wrappers stay on disk
  (M4 deletes them).
- **Wrapper hardening found while building the X stack** (all three
  belong in the wrappers, and the gcc specs had handled them):
  1. start files / `-lc` / builtins are added only on **link**
     invocations — meson/autoconf probe the compiler with
     `clang -E -x c - -v`, and preprocessing the ELF crt objects dumped
     ~1 GB of garbage per probe (a real meson configure hang/failure).
  2. `-shared` links get crti.o/crtn.o but **no Scrt1.o** (musl's
     `%{!shared: Scrt1.o}`) — Scrt1's `_start_c` leaves an undefined
     `main` that meson's `-Wl,--no-undefined` rejects.
  3. `-nostdinc` also drops clang's resource headers (`cpuid.h`,
     `mmintrin.h`, …), which musl doesn't ship — the wrappers add
     `-isystem $(clang -print-resource-dir)/include` after the musl
     include dir (musl stays authoritative; compiler-only headers fill
     the gaps).
- **x11-shared-build.sh fixes**: libXfont2's `noinst_PROGRAMS`
  (`test/utils/lsfontdir`) link against the library's
  hidden-visibility internals, so the full `make`/`make install` fail
  once `-fvisibility=hidden` is active (the configure probe passes
  under clang) — the build configures then installs only the `.la` +
  `.pc`. The `log libsha1 (static…)` / `log xkbcomp (dynamic)` lines
  were unquoted-paren bash syntax errors that silently truncated every
  full-script run after pixman. Run with `SHARED_ONLY=1` (the static
  archives are not staged; meson's `--default-library both` misbehaves
  under clang).
- **Structural bug fixed**: `mktoybox.sh` always re-installed into
  `.build/toybox-root` and ended with `rm -f toybox64`, so the static
  recovery build clobbered the dynamic staging (userland64 copied a
  STATIC `System/Tools/toybox`; lint caught it). `TOYBOX_STAGE` knob:
  the dynamic build installs into `.build/toybox-root`, the recovery
  build into its own scratch.
- **Acceptance**: m1_boot green (whole dynamic world now clang-built),
  m2_xfbdesk green (clang Xfb + the 9 clang x11 `.so`'s + clang
  xkbcomp), m4_recovery all four modes green (forced mode needs the
  param kernel: `rm .build/64/kreal64.o && make buildfnx
  FNX_RECOVERY_PARAM=1 && ./tools/mkesp.sh`), m0_clang_hellos green,
  fshlint 0 (21 ELFs, staged System/Tools/toybox dynamic again).
  Provenance: `.comment` on the unstripped ELFs shows "clang version
  19.1.7" (dash64, recovery-sh, Xfb, xkbcomp, all nine `.so`'s,
  libconfig.so.1); the always-present "GCC:" line is musl's gcc-built
  crt objects merging at link — gone when musl itself is clang-built
  (M2).

Next: **M2** — build musl + libc++/libc++abi/libunwind with clang
(`musl64` configure `CC=clang`, the llvm-cxx cmake recipe with the
compilers swapped, `tools/musl-clang++64.sh`); the crt `.comment`
footprint and the recovery set then become fully clang. musl's own
`make`/`configure` uses `CC=gcc` today via config.mak, untouched by
M1.

### M2 — DONE (commit 5db8cbb)

- **musl built by clang**: the `musl64` recipe configures
  `CC=/usr/lib/llvm-19/bin/clang` and overrides config.mak
  `LIBCC = $(CURDIR)/.build/compiler-rt/.../libclang_rt.builtins-x86_64.a`
  (absolute - musl's link runs inside third_party/musl - in place of
  `-lgcc -lgcc_eh`; clang has no libgcc and musl's configure would
  otherwise pick up the host gcc's glibc libgcc). musl's build is
  self-contained (own headers, freestanding) so no sysroot/-isystem is
  needed; clang's configure-generated CFLAGS_AUTO drops the gcc-only
  flags. libc.so/crt1.o/Scrt1.o/libc.a now carry pure clang `.comment`.
- **C++ runtimes by clang++**: the `llvm-cxx` recipe now uses
  `CMAKE_C_COMPILER=$(MUSL64_CC_STATIC)` and
  `CMAKE_CXX_COMPILER=tools/musl-clang++64.sh` (no more gcc/-specs
  flags; the wrapper encodes the contract). The wrapper is
  self-bootstrapping: it adds the libc++ include dir and
  `-lc++ -lc++abi -lunwind` only once `.build/llvm-cxx-prefix` is
  populated, so the runtimes build with it. libc++/libc++abi/libunwind
  are now pure clang.
- **tools/musl-clang++64.sh** replaces musl-g++64.sh (`MUSL64_CXX`):
  static clang++ with the same driver contract as the C wrapper plus
  libc++/libc++abi/libunwind, `-Wl,--eh-frame-hdr` (libunwind needs
  .eh_frame_hdr), and compiler-rt **crtbegin.o/crtend.o** - the
  standalone builtins cmake does not emit crt objects, and clang++
  links need crtbegin for `__dso_handle` (libc++ locale/guard code
  references it). Compiler-rt crtbegin/crtend sources are compiled with
  the static wrapper into `.build/compiler-rt/lib/linux/` by new
  Makefile rules. Two wrapper gotchas: libc++'s own include dir MUST be
  searched before the C headers (its `<cstdio>`/`<stdio.h>` wrappers
  `#include_next` the C library's) and the C++ archives must precede
  `-lc` in the static link (libc++abi references pthread symbols that
  live in musl's libc).
- **Acceptance** (all green): in-guest `cpp_smoke` (`M2_CPP_SMOKE:
  True` - exceptions, RTTI, string/vector, iostream, std::thread via
  the static clang musl + clang libc++ stack; the same binary runs on
  the build host), m1_boot + m4_recovery all modes on the clang-built
  musl, m2_xfbdesk (the M1-era x11 `.so`'s load against the new
  clang-built libc.so - ABI clean), fshlint 0. Every staged ELF now
  carries pure clang `.comment` - the gcc crt footprint is gone.

Next: **M3** — the kernel via clang + lld: CC64R flags verbatim
(compile64 pattern), **PATCH_PIC gated** (clang emits `lea` for hidden
data; gcc-14 needs the byte patch), EFI link ported to lld/llvm-objcopy.

### M3 — DONE (commit PENDING; see git log)

**The kernel builds with clang 19** (CC64K/CC64 = `$(CLANG19)`, the gcc
CC64R flag set kept verbatim; `CLANG19`/`LLVM_OBJCOPY` vars at Makefile
top). Acceptance on the final image: m0/m1/m2/cpp + m4_recovery all four
modes green, fshlint 0, no kernel exceptions.

- **Source fixes the clang kernel needed** (gcc warned where clang errors
  under `-std=c89`):
  - `newstat/newfstat/newlstat.c`: forward-declare `fill_new_stat` (the
    c89 implicit declaration → "conflicting types" error). Decl is
    placed BEFORE the `#ifdef __DEBUG__` block (inside it, non-debug
    builds still failed).
  - `drivers/net/virtio_net.c` + `kernel64/asm64.c`: `"Nd"(port)` port-I/O
    constraints make clang emit `inb %edx,%al` (invalid); use
    `"d"((unsigned short)(port))` (the inw/outw helpers already did).
  - `drivers/usb/usb-hub.c`: `kfree((addr_t)st/hubdesc)` (kmalloc results
    cast to `unsigned char *`; kfree takes `addr_t` - pointer→int
    conversion is an error under clang).
  - `drivers/usb/xhci.c`: bare `return;` in the int-returning
    `xhci_enumerate` → `return -EIO;` (-Wreturn-mismatch).
  - `net/unix.c` + `include/fnx/net/unix.h`: `unix_ioctl`'s 4th arg
    `unsigned int` → `addr_t` (proto_ops.ioctl type; gcc only warned).
  - `net/af_packet.c`: `packet_write/read` are 4-arg but were aliased to
    the 5-arg `.send`/`.recv` slots (gcc warned); added thin
    `packet_send/recv` adapters dropping the flags.
- **PATCH_PIC stays ON for clang** (the plan's "gated off" guess was
  wrong): clang ALSO addresses -fPIC extern data via
  R_X86_64_REX_GOTPCRELX, and GNU ld's PE link cannot relax it (no GOT).
  Without the patch the kernel #GP'd in mem_init's `_last_data_addr`
  access (a zeroed GOT slot). Verified with a byte-level repro.
- **The m2 X11 regression root cause (clang-only)**: when clang takes the
  ADDRESS of a hidden extern and CSEs it, it can FOLD the GOT-slot load
  into an ALU memory operand - `cmp [rip+sym],reg` / `add [rip+sym],reg`
  (5 sites kernel-wide). PATCH_PIC only rewrites the `mov` form, and the
  PE link resolves the folded form as a DIRECT memory access to the
  symbol's CONTENTS (no GOT). The first site hit: `do_exit`'s wait4
  wakeup check `p->sleep_address == &sys_wait4` never matched → Xfb's
  wait4 (Pclose of the xkbcomp child) slept forever → the X desktop hung
  at ActivateDevice(keyboard). Fixed by explicit
  `__attribute__((visibility("hidden")))` on the three affected
  declarations (`sys_wait4` in syscalls.h, `sys_utsname` in utsname.h,
  `inotifyfs_fsop` in fs_inotify.h) - an explicit decl attribute makes
  clang emit the direct PC32 `lea` (the compile-flag `-fvisibility=hidden`
  alone does NOT; no flag combination removes the GOTPCRELX). The 1137
  remaining `lea sym@GOTPCRELX(%rip),reg` + `mov (%reg),reg2` pairs are
  BENIGN in the PE (lea resolves to `&sym`, mov reads the value). If a
  future clang build folds a new site, extend the attribute list.
- **objcopy → llvm-objcopy**: `$(LLVM_OBJCOPY) --remove-section .comment
  --subsystem=efi_application` produces an identical PE (subsystem 0xa).
  Verified booting.
- **The EFI link stays on GNU ld (plan amendment)**: `ld.lld -m i386pep`
  and `lld-link` require COFF object inputs; the FNX kernel is ~400 ELF
  `.o`'s that GNU ld's BFD happily links into PE32+. lld has no
  ELF-objects→PE output path. Porting would mean converting every kernel
  object to COFF (llvm-objcopy per-object) - parked as a later item.
- **Two image fixes surfaced while diagnosing m2**: the FSH skeleton now
  stages `/tmp` (Xfb's unix-listener mkdir needed it; errno 2), and
  `xfbdesk-root` re-copies `$(XFB_BIN)` so the desktop image can't go
  stale against a rebuilt Xfb.
