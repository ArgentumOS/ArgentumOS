# LLVM C++ runtime + C++ capability plan (via the GCC wrapper)

Status: PLAN — for execution. Adds C++ to the FNX native x86-64 userland
by linking the **permissively-licensed LLVM C++ stack** (libc++, libc++abi,
libunwind) into the existing **GCC/musl wrapper**, then proves it with a
static C++ binary running under FNX in QEMU.

**Execution status (2026-09): P0–P3 DONE.** P0+P1 built static
libc++/libc++abi/libunwind (pin llvmorg-19.1.7, see §6.3) against
`.build/musl64`; P2 added `tools/musl-g++64.sh` (+ Makefile `MUSL64_CXX`);
P3 `cpp_smoke` runs green under FNX (`CPP-SMOKE: all checks OK`, no kernel
exceptions). Measured gotchas recorded in §P1/§P2/§6.3 below. P4 (FLTK)
is tracked by docs/fltk-port-plan.md.

Scope note: the kernel (gcc, `CC64`) and the C userland (gcc via
`tools/musl-gcc64.sh`) are untouched by this plan. Only the *userland
toolchain's* C++ capability is added. The C++ standard library is the LLVM
one, *not* libstdc++, because LLVM's C++ libraries are MIT / Apache-2.0
(with LLVM exception) — fully compatible with FNX's MIT tree, with no GPL
anywhere, even if modified and vendored.

---

## 1. Long-term toolchain direction (note for the record)

FNX's long-term self-hosting plan is to make **LLVM/Clang the compiler
toolchain** for the whole OS and **musl the C standard library**:

- Clang, lld, compiler-rt, libc++, libc++abi and libunwind are all
  Apache-2.0-with-LLVM-exception (libc++ is additionally MIT) — one
  permissive roof over kernel + userland, vendorable into the MIT tree
  with no GPL components.
- A single LLVM toolchain gives one exception/unwinding implementation
  (LLVM libunwind end-to-end) and one ABI story across kernel and
  userland, removing the GCC↔LLVM unwind-integration hazard entirely.
- musl is already the userland libc (`.build/musl64`); the kernel stays
  freestanding but can also build with clang.

The steps below therefore land **today's GCC-wrapper C++ capability in a
way that does not need to be redone at the Clang migration**: the C++ ABI
and runtime choices (exceptions, RTTI, static linking, libc++/abi/unwind)
are identical under clang. When self-hosting arrives, the wrapper swap is
`gcc`/`g++` → `clang`/`clang++` with the same `--target=x86_64-linux-musl`
sysroot and the same runtime libraries.

---

## 2. Current state (verified)

- Userland C toolchain: `tools/musl-gcc64.sh` — `gcc -static
  -specs $(MUSL64_PREFIX)/lib/musl-gcc.specs`, musl installed at
  `.build/musl64` (built from `third_party/musl` by the `musl64` Makefile
  target: `./configure --target=x86_64 --prefix=$(CURDIR)/.build/musl64`).
- `.build/musl64/lib` contains `libc.a`, `libm.a`, `libpthread.a`, etc. —
  **no libstdc++**, and no `g++` wrapper exists (C only today).
- Host tooling available: `gcc/g++ 14.2`, `cmake 3.31.6`; **no ninja**
  (use `-G "Unix Makefiles"`); no clang installed on the host (fetched
  later if/when needed).
- The musl build is static-only: FNX has no dynamic linker, so everything
  must be `-static`.
- Build wiring lives in the top `Makefile`: `MUSL64_PREFIX`,
  `MUSL64_SPECS`, `MUSL64_CC`; apps staged into `.build/rootfs64` and
  packed by `make rootdisk64`.

---

## 3. Design decisions

1. **C++ standard library = LLVM libc++/libc++abi/libunwind** (not
   libstdc++). Permissive licensing; clean vendoring; identical runtime
   under the future clang toolchain.
2. **Compile with the existing GCC** (host gcc/g++ against the musl
   sysroot) for now. Exceptions/RTTI enabled; libc++ on musl is a
   supported, common configuration.
3. **Static-only**: `*_ENABLE_SHARED=OFF` for all three libraries; link
   fully static into FNX binaries.
4. **One unwind implementation**: LLVM libunwind provides `_Unwind_*` for
   everything in C++ userland; do not mix with GCC's libgcc `_Unwind`
   personality (link `-lunwind` explicitly and consistently).
5. **Do not commit LLVM to git.** Follow the OVMF precedent
   (`tools/fetch-ovmf.sh` → `.build/ovmf`): a pinned-release fetch into
   `.build/` (recorded tag + hash). Only the *built* static archives and
   headers are inputs to the build; if anything must be tracked, track the
   fetch script and the pin.
6. **Define the userland C++ ABI baseline once** (this plan): `x86-64
   linux-musl`, Itanium ABI via libc++abi, LLVM libunwind, `-fexceptions
   -frtti`, `-static`. Future apps and the Clang migration inherit it.
7. Kernel stays on `CC64` (gcc) for now; clang kernel build is an
   out-of-scope future step of §1.

---

## 4. Phases

### P0 — Fetch the LLVM sources (pinned, into `.build/`)

- Add `tools/fetch-llvm.sh` that downloads a pinned llvm-project release
  tarball into `.build/llvm-src` (record the tag + sha256 in the script).
- We need only: `libcxx`, `libcxxabi`, `libunwind`, `cmake/`, `runtimes/`
  (sparse extraction or full tarball — source is tens of MB).
- **Effort: ~15 min + ~100 MB.**
- **Verify:** `ls .build/llvm-src/libcxx/include/cstdio` etc.

### P1 — Build the static runtimes against musl

Configure with CMake from the `runtimes` dir, targeting the `.build/musl64`
sysroot (host gcc/g++ as the compilers):

```
cmake -G "Unix Makefiles" -S .build/llvm-src/runtimes -B .build/llvm-cxx \
  -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_SYSROOT=$PWD/.build/musl64 \
  -DCMAKE_INSTALL_PREFIX=$PWD/.build/llvm-cxx-prefix \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIBCXX_ENABLE_SHARED=OFF -DLIBCXXABI_ENABLE_SHARED=OFF \
  -DLIBUNWIND_ENABLE_SHARED=OFF \
  -DLIBCXX_ENABLE_STATIC=ON -DLIBCXXABI_ENABLE_STATIC=ON \
  -DLIBUNWIND_ENABLE_STATIC=ON \
  -DLIBCXX_HAS_MUSL_LIBC=ON
cmake --build .build/llvm-cxx -j$(nproc)
cmake --install .build/llvm-cxx
```

Adjust flags to the pinned LLVM version's documented musl knobs (the
`LIBCXX_HAS_MUSL_LIBC` / sysroot spelling drifts between releases; consult
`libcxx/docs/UsingLibcxx.rst` for the release). The kernel-headers include
(`tools/kernel-headers`) must not leak into this build; the sysroot alone
is correct.

**Measured (2026-09, pin 19.1.7):**
- `-DCMAKE_SYSROOT` is NOT applied by CMake for a same-arch host gcc, and
  plain `--sysroot` fails anyway (gcc looks in `$sysroot/usr/include`, musl
  installs headers in `$prefix/include`). Instead pass
  `-DCMAKE_C_FLAGS/-DCMAKE_CXX_FLAGS="-static -specs <musl64>/lib/musl-gcc.specs"`
  (plus `-I tools/kernel-headers`) and `-DCMAKE_EXE_LINKER_FLAGS=-static`.
- libc++ needs `<linux/futex.h>` (std::atomic wait): added a minimal
  `tools/kernel-headers/linux/futex.h` with the FUTEX_* constants (FNX has
  no futex syscall; runtime calls just ENOSYS).
- Built archives: `.build/llvm-cxx-prefix/lib/{libc++,libc++abi,libunwind}.a`
  + headers under `include/c++/v1`.

- **Effort: 1–3 h** (mostly first-configure flag debugging).
- **Verify:** `.build/llvm-cxx-prefix/lib/libc++.a`, `libc++abi.a`,
  `libunwind.a`; headers under `.build/llvm-cxx-prefix/include/c++/v1`.

### P2 — Extend the wrapper to C++

- Add `tools/musl-g++64.sh` mirroring `tools/musl-gcc64.sh`:
  ```
  exec g++ -static -I"$(dirname "$0")/kernel-headers" \
    -specs "$MUSL/lib/musl-gcc.specs" \
    -nostdinc++ -isystem "$PREFIX/include/c++/v1" \
    -nostdlib++ -Wl,--eh-frame-hdr \
    "$@" -L "$PREFIX/lib" -lc++ -lc++abi -lunwind -lm
  ```
  (`g++` compiles C++ with the host libstdc++ headers suppressed by
  `-nostdinc++`, and the host libstdc++ runtime by `-nostdlib++`; runtime
  comes from the LLVM libs. `-lunwind` must follow `-lc++abi`.)
- **Measured gotcha (2026-09):** `-Wl,--eh-frame-hdr` is REQUIRED. The
  musl-gcc.specs `*link` overrides gcc's default link spec, which normally
  passes `--eh-frame-hdr`; without the `.eh_frame_hdr` section LLVM
  libunwind cannot find FDEs and every throw goes uncaught
  ("terminating due to uncaught exception").
- Add Makefile vars alongside `MUSL64_CC`: `MUSL64_CXX`, plus the
  `llvm-cxx` phony target (P0+P1, DONE 2026-09) that `userland64`
  depends on: fetch → cmake configure → `cmake --build/--install` →
  `.build/llvm-cxx/.installed` stamp. Flags use the **absolute** specs
  path (`$(CURDIR)/$(MUSL64_SPECS)`) — cmake's try-compiles run in the
  build dir, so a relative `-specs` path fails there; and the stamp
  depends on `.build/llvm-cxx/Makefile`, not `CMakeCache.txt` (a failed
  configure still writes the cache).
- **Effort: ~30 min.**
- **Verify:** `tools/musl-g++64.sh -v` prints a clean driver invocation;
  host `file` on a trivial `int main(){return 0;}` output shows `statically
  linked` ELF64 against musl (`ldd` → "not a dynamic executable").

### P3 — C++ smoke test under FNX

- Add `tools/cpp_smoke.cpp` (or `userland/cpp_smoke.cxx` in rootfs):
  exercises `throw`/`catch`, RTTI (`dynamic_cast`), `std::string`,
  `std::vector`, iostream, and a `std::thread` (musl pthreads), each
  printing `CPP-OK: <name>` on stdout.
- Stage to `.build/rootfs64/bin`, rebuild `rootdisk64`, boot in QEMU
  (existing `.build/*_run.sh` boot harness style) and assert every
  `CPP-OK` line appears and there are zero kernel exceptions.
- **Effort: ~half a day.**
- **Acceptance for this plan:** the smoke binary runs under FNX with
  exceptions, RTTI, iostream, threads all green.

### P4 — (later, gated) Real C++ toolkit demo

- Only after a GUI toolkit is chosen: compile it with `MUSL64_CXX`, fix
  any libc++/musl gaps it trips over (locale stubs, `dl*`, etc.), and land
  the first C++ app in the root image.
- **Effort: depends entirely on the toolkit; tracked separately.**

---

## 5. Risks / gotchas

- **Unwind mixing:** the classic failure is combining LLVM libunwind with
  GCC's libgcc `_Unwind` — silent corruption on the first throw. Keep one
  implementation; check `nm` output for a single `_Unwind_*` provider.
- **Static-only:** any shared variant of the trio built accidentally will
  not load on FNX (no dynamic linker). Enforce `*_ENABLE_SHARED=OFF` and
  check for `.so` outputs before installing.
- **musl gaps libc++ may touch:** `locale`, `__cxa_guard`, TLS, `pthread`
  edge cases — the smoke must cover them; some need
  `-D_LIBCPP_HAS_NO_...` config if the pinned musl lacks a facility.
- **ABI ossification:** every C++ decision made now (exception model,
  RTTI, unwind) becomes the userland ABI; changing it later means
  rebuilding all C++ apps. §3.6 fixes it once.
- **Version drift:** LLVM's cmake knobs change between releases; pin and
  record the tag+hash in `tools/fetch-llvm.sh` and re-verify flags on
  upgrade.

---

## 6. Open questions (decide before/at P2)

1. Keep the C++ capability out of the default `make userland64` until P3
   passes, or wire it in immediately? (Answered 2026-09: P3 passed, so
   `cpp_smoke` is wired into `userland64` via `MUSL64_CXX`.)
2. Do we compile the kernel with clang as part of the self-hosting plan
   soon, or defer until the C++ userland is proven? (Default: defer; §1.)
3. Pin the LLVM release: **llvmorg-19.1.7** (answered 2026-09). The
   "newest stable" default is overridden by measured evidence: libc++ ≥ 20
   (verified at 23.1.0) calls Clang-only builtins (`__is_bounded_array`,
   `__builtin_operator_new`, …) with no `__has_builtin` fallback, so **GCC
   cannot compile it**; llvmorg-19.1.7 is the verified last line carrying
   GCC fallbacks (is_array.h `#if __has_builtin(__is_array)` + manual
   impls). Upgrade the pin at the Clang migration (§1).

---

## 7. Milestones

- **M1 (P0+P1):** static `libc++.a`/`libc++abi.a`/`libunwind.a` +
  headers built against `.build/musl64`.
- **M2 (P2+P3):** `tools/musl-g++64.sh` compiles `cpp_smoke`, which runs
  green under FNX in QEMU.
- **M3 (P4):** first chosen C++ GUI toolkit app boots on the FNX desktop.
- **Future (§1):** clang/libc++/musl self-hosted toolchain replaces the
  GCC wrapper; kernel + userland built with clang under one permissive
  roof.
