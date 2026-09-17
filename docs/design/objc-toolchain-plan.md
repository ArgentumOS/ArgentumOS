# Objective-C for the Argentum OS — plan (clang frontend + the libobjc2 runtime)

Status: **DRAFT (2026-09), for review. P1 PASSED on 2026-09-17** — libobjc2
builds for musl with our clang wrappers and the runtime *works*: classes register
across translation units, and blocks, ARC, categories, protocols, properties,
`@try/@catch`, `@synchronized` and autorelease pools all behave. §8 holds the
measured record (including one false alarm it retracts). Next: **P2** — the
wrapper's ObjC mode and the `objc_smoke` probe.

Direction, decided by the user (2026-09-17): **Objective-C becomes a language of
the system build** — clang's frontend, the **libobjc2** runtime — *and* it is the
language the **Cocoa-parity class library is written in**. That second half is
the job the parked C++ Argentum UIKit was being built for
(`docs/design/argentum-uikit-plan.md`, DEFERRED; branch `park/argentum-uikit`).

No doctrinal change is needed. The OS profile already says languages are open
per subsystem, naming this one: *"Languages are open per subsystem (C, C++,
Objective-C as the task demands) — no runtime beyond what the language needs"*
(`docs/reference/os-profile.md`). What this plan adds is the **runtime source**,
which is the only piece the system does not have.

## 1. What this is, and what it is NOT

**Is:**

- the Objective-C language and its runtime, usable by any subsystem (tools,
  daemons, libraries);
- the vehicle for the **Cocoa-parity class library** — the U-series catalog in
  `docs/design/cocoa-parity-plan.md` stays the inventory of what to build;
- the substrate for a **clean-room first-party Foundation**: no code derived from
  GNUstep or ObjFW (the user's decision, 2026-09-17). `docs/design/foundation-plan.md`
  is where that work is planned, and where the wall around it is recorded.

**Is NOT:**

- **GNUstep.** `docs/archive/gnustep-evaluation.md` REJECTED it — *"I don't want
  to own a GNUstep fork"* — because base/gui/plists on musl would be ours to
  sustain alone. That rejection stands, and it is about the *framework*. The
  **runtime** is a different, much smaller thing (§3), and the **language** a
  third. (The record's Alpine line is corrected in §5.)
- **Apple's runtime** — not available, not free.
- **GCC's `libobjc`** — the old (`gcc`) ABI, and **GPL**: excluded by the
  licence rule before it is excluded by anything else.

## 2. Verified state (measured on this tree, 2026-09-17)

Everything here was run through the **actual** wrappers
(`tools/musl-clang64.sh` / `musl-clang++64.sh`), not assumed.

| # | check | result |
|---|---|---|
| 1 | ObjC **frontend** present in clang-19? | **yes** — `musl-clang64.sh -c x.m` compiles for the musl target *today*, with no toolchain change |
| 2 | language selection | `.m` extension is inferred; `-x objective-c` also works |
| 3 | `-fobjc-runtime=` values clang accepts | `gcc`, `gnustep-1.7`, `gnustep-1.8`, `gnustep-1.9`, **`gnustep-2.0`**, `objfw` |
| 4 | the **default** ABI | `gcc` — i.e. the GPL runtime's ABI; the flag is therefore mandatory, not optional |
| 5 | `-fobjc-arc` and `-fblocks` | accepted; full modern ABI emitted |
| 6 | what a link is missing (`gnustep-2.0` + ARC + blocks) | `objc_msgSend`, `objc_storeStrong`, `objc_retainBlock`, `__objc_load`, `_NSConcreteGlobalBlock` |
| 7 | what a link is missing (default `gcc` ABI) | `objc_msg_lookup`, `objc_lookup_class`, `__objc_exec_class` |
| 8 | ObjC **headers** in clang's resource dir? | **no** — there is no `objc/` there; `objc/objc.h`, `objc/runtime.h`, `objc/message.h` must come from the runtime |
| 9 | a usable runtime on the host? | **no** — `/usr/include/objc` absent; only `libobjc.so.4` / `libobjc_gc.so.4` (GCC, GPL) |
| 10 | sections clang emits | `__objc_classes`, `__objc_class_refs`, `__objc_selectors`, `__objc_constant_string`, `.text..objcv2_load_function` (the module constructor → `.init_array`) |
| 11 | **the linker's half of the ABI** | **satisfied** — declaring `__objc_classes` / `__objc_cats` in an object and linking through the FNX contract resolves `__start___objc_classes`, `__stop___objc_classes`, `__start___objc_cats`; only sections *not* present are undefined. The `__start_`/`__stop_` symbols the `gnustep-2.0` ABI depends on **are** synthesized by the ld FNX already uses |

**Conclusion:** the gap is **one runtime and its headers**. Nothing about the
compiler, the linker, the ABI, or the language blocks Objective-C on FNX.

One measured wrapper gotcha (recorded so it is not rediscovered): **`-x
objective-c` leaks onto the wrapper's trailing `libclang_rt.builtins-*.a`**, and
clang then tries to parse the archive as source. Use the `.m` extension, or emit
`-x none` before the trailing archives.

## 3. Design decisions

1. **Runtime = `libobjc2`** (GNUstep's Objective-C runtime), used with
   **`-fobjc-runtime=gnustep-2.0`**. It is the only free, clang-oriented ObjC
   runtime: **MIT** — the README is explicit that the GPL'd GCC code was removed,
   *"allowing the entire work to be MIT licensed"*. It is a drop-in replacement
   for the GCC runtime "intended for use with Clang", and the 2.0 ABI is what
   carries ARC APIs, blocks, `@synchronized`, associated references, fast
   enumeration and synthesized properties.

   *Licence caveat to record:* some distribution spec files still say GPL — a
   stale carry-over from before the GCC code was removed. The authority is the
   upstream `COPYING` file, not a distro spec.

2. **The wrapper grows an ObjC mode.** Feed it `.m`/`.mm` (so no `-x` is
   needed), and add: the runtime's include dir (`objc/…`), the ABI flag
   `-fobjc-runtime=gnustep-2.0`, and `-lobjc`. `.mm` (Objective-C++) then rides
   the existing libc++ stack for free. `-x` must not be used without a
   `-x none` before the trailing archives (see §2).

3. **Staging: `libobjc.so.N` in `/System/Libraries/`**, under the same
   soname/glob rule as libconfig, libc++ and the X stack — nothing new in shape.

4. **No separate `libBlocksRuntime`.** libobjc2 **ships its own blocks runtime**;
   upstream warns explicitly not to link both (`_NSConcreteGlobalBlock` and
   friends would be multiply defined). This decision is load-bearing and cheap to
   get wrong.

5. **Build it against `.build/musl64` with the same clang** the rest of the
   userland uses, `-DTESTS=OFF -DOLDABI_COMPAT=OFF -DLLVM_OPTS=OFF` (we want
   neither the legacy ABIs, the upstream test suite, nor the optional LLVM
   optimisation plugin). Its C++ exception-interop pieces must pair with
   **libc++abi**, not libsupc++ — one `_Unwind_*` provider, as with C++ (§5).

6. **Fetch and pin like the LLVM runtimes do**: a `tools/fetch-libobjc2.sh` into
   `.build/libobjc2-src` with the tag and sha256 in the script (the
   `tools/fetch-llvm.sh` pattern). `robin-map` — a header-only **MIT** map that
   libobjc2 needs — must be pinned too; upstream it is a submodule, and its
   absence is the classic build failure.

7. **The class library is first-party.** libobjc2 gives the *language and
   runtime*; it does **not** give `NSObject`/`NSString`/`NSArray`/Foundation (that
   was GNUstep's job, and GNUstep is rejected and stays rejected). The
   Cocoa-parity catalog is the inventory; the code is ours.

8. **The toolkit direction (the user's decision, 2026-09-17).** The Cocoa-parity
   class library is written **in Objective-C, on libobjc2, first-party**. The
   parked C++ UIKit becomes the *reference specimen* — its settled decisions and
   measured costs (one arithmetic for draw and hit-test; the damage region; the
   event-intake fix; the gate lessons) carry over as requirements, in
   `docs/design/argentum-uikit-plan.md` §0a.

## 4. Phases

### P0 — Fetch and pin (`.build/`)

- `tools/fetch-libobjc2.sh`: pinned tag + sha256 into `.build/libobjc2-src`;
  fetch/pin `robin-map` alongside.
- **Verify:** the tree exists and the pin matches.

### P1 — Build libobjc2 against musl. THIS IS THE RISK GATE.

- CMake against `.build/musl64` + the clang/libc++ stack; install to
  `.build/objc-prefix`.
- **Verify:** the archive exists; `nm` shows the ABI's exports
  (`objc_msgSend`, `objc_storeStrong`, `objc_retainBlock`, `__objc_load`,
  `_NSConcreteGlobalBlock`); exactly one `_Unwind_*` provider; no `.so` that the
  FNX loader cannot use.
- **Green here kills the one unproven assumption in this plan.** Do not plan
  past P1 until it passes.

### P2 — Wrapper ObjC mode + `objc_smoke`

- Wrapper flags (§3.2); a guest probe exercising what the ABI promises: a class,
  a category, a protocol, `@property`/`@synthesize`, a message send, **blocks**,
  **ARC**, `@try/@catch`, `@synchronized` — each printing a marker (the house
  probe style, self-checking, exit non-zero on failure).

### P3 — Guest gate

- `tests/cases/objc_smoke.py`: boot, run the probe, assert the markers (and, if
  the toolkit half follows, the first ObjC window).

### P4 — The class library, in Objective-C (the parked UIKit's job, restated)

- **Planned in `docs/design/foundation-plan.md`** — the clean-room first-party
  Foundation: core first (object + memory + collections), ARC as the house
  default, house names, and a wall around GNUstep/ObjFW that is enforced by a
  build gate. Those are the user's decisions of 2026-09-17.
- Its inventory stays `docs/design/cocoa-parity-plan.md`'s U-series, and its
  requirements record stays `docs/design/argentum-uikit-plan.md` §0a. **This is a
  large, separately-gated effort, not a phase of the toolchain work** — P0–P3 are
  what make it possible.

### P5 — Self-hosting

- A `docs/design/self-hosting-packages.md` §6 entry for libobjc2 + robin-map
  (standing policy): licence, pin, musl + clang recipe, and what the on-FNX
  rebuild path needs.

## 5. Risks / gotchas

- **musl support is UNPROVEN, and the record currently overclaims it.**
  `docs/archive/gnustep-evaluation.md` says *"musl can host GNUstep (Alpine is
  existence proof)"*. That is not substantiated: Alpine's `libobjc` in `main` is
  **GCC's** (GPL), not libobjc2, and GNUstep's own issue tracker
  (`gnustep/libs-base#356`) shows it poorly tested on musl (ICU/locale failures;
  the reporter used GCC + vanilla libobjc). libobjc2 is a smaller target than
  gnustep-base, but this must be **measured at P1**, not assumed.
- **Executable memory for dispatch.** libobjc2 allocates executable memory for
  dispatch thunks on some paths. FNX enforces no W^X today (`tests/COVERAGE.md`
  item 16), so this works — but `docs/design/security-hardening-plan.md` H1
  (NX/W^X) is precisely the change that would break it. Verify at P1/P2 and
  record the interaction; if a non-JIT path exists, prefer it.
- **Custom sections must survive the link.** The `__objc_*` sections are not
  garbage. If `--gc-sections` is ever added to the userland link they need
  `KEEP` (or `-Wl,-u,__objc_load`). Today the link does not use it, and §2.11
  shows the section symbols resolve.
- **Unwind/provider mixing.** The classic failure (see the C++ plan) is mixing
  LLVM libunwind with another `_Unwind` provider — silent corruption on the first
  throw. libobjc2's EH interop must pair with **libc++abi**.
- **Licence hygiene, both directions.** Record the upstream `COPYING` (MIT), not
  the stale distro specs; and confirm `robin-map`'s MIT text before pinning.
- **ABI ossification.** `-fobjc-runtime=gnustep-2.0` + the libobjc2 soname become
  part of the userland ABI; changing either later rebuilds everything written on
  them. Decide once (§3.1).
- **`-no-pie`/ET_EXEC model.** FNX's loader rejects PIE mains; the existing
  wrapper already handles this for C/C++ and the ObjC path inherits it — but a
  runtime that defaults to PIC/PIE build settings needs `-fPIC` in the *library*
  build and the fixed-base model in the *executable* link, exactly as the other
  shared stacks do.

## 6. Open questions (yours to answer)

1. **ARC or MRR** as the house default? ARC is the reason the 2.0 ABI exists; MRR
   is more explicit and easier to reason about in a small system.
2. **Names.** Keep the Cocoa-parallel names the parked catalog used (`View`,
   `Window`, `Button`) for the ObjC class library, or rename?
3. **Supersede or coexist?** Does the ObjC toolkit replace the parked C++
   toolkit permanently? (The C++ *toolchain* stays either way — `cpp_smoke`,
   libc++ — it is not in question.)
4. **ARC vs the collector.** `docs/reference/os-profile.md` records a
   conservative-GC experiment (`GC_USE_LD_WRAP`) for allocation-tangled code.
   ARC and a conservative collector are different ownership worlds; which wins
   where?
5. **Concurrency.** Upstream `libdispatch` is optional; is a small first-party
   concurrency layer preferable to importing GCD?

## 7. Milestones

- **M1 (P0+P1):** libobjc2 built against `.build/musl64`, exports verified. The
  feasibility question is answered.
- **M2 (P2+P3):** the wrapper compiles and links ObjC; `objc_smoke` is green
  under FNX in QEMU.
- **M3 (P4, first slice):** the first ObjC class-library slice (the U-series'
  first entry) draws on Xfb under a guest gate.
- **M4 (P5):** the self-hosting manifest entry lands.

## 8. P1 outcome (measured, 2026-09-17) — the risk gate PASSES

Run against pinned libobjc2 **v2.3** (`e877e782`) + robin-map **v1.4.1**. What
landed: `tools/fetch-libobjc2.sh` (both pins by **commit**, not tag) and an
explicit `make libobjc64` target in `mk/10-toolchain.mk` that is deliberately
**not** in `userland64`'s dependency chain.

### Green

- **It builds against musl** with the same clang wrappers as the rest of the
  userland, installing to `.build/objc-prefix` (`include/objc/…`,
  `lib/libobjc.so.4.6` + `libobjc.so`, plus `libobjc.a`).
- **Two port fixes were required** (`third_party/libobjc2-fnx.patch`, applied
  idempotently by the recipe):
  1. `LINKER_LANGUAGE C` → `CXX`. Upstream links the runtime with the C driver;
     a glibc `cc -shared` silently supplies `crtbeginS.o`, but the FNX C wrapper
     deliberately adds no crt objects to a `-shared` link, so the link died on
     `undefined reference to __dso_handle`. The runtime contains C++
     (`objcxx_eh.cc`) whose EH must pair with OUR libc++abi, so the C++ driver is
     the correct linker here.
  2. the **static** target must link `tsl::robin_map` too, or
     `selector_table.cc` fails with `'tsl/robin_set.h' file not found`.
- **The ABI is complete and correctly wired**: `objc_msgSend`,
  `objc_storeStrong`, `objc_retainBlock`, `__objc_load`, `_NSConcreteGlobalBlock`,
  `objc_sync_enter`, `objc_exception_throw`, `objc_autoreleasePoolPush/Pop` all
  defined; `NEEDED` = exactly `libc++.so.1`, `libc++abi.so.1`, `libunwind.so.1`,
  `libc.so` — **one `_Unwind_*` provider, no `libgcc_s`**, so §5's mixing risk is
  closed for this library. It even carries a real `SONAME` (`libobjc.so.4.6`):
  upstream's `set_property(TARGET PROPERTY NO_SONAME true)` is a malformed no-op,
  so the ordinary staging pattern applies.
- **Runtime features work.** A probe linked against the *shared* runtime and run
  reported correct results for blocks (stack block + `Block_copy` to heap +
  invoke), ARC's block management, `@synchronized`, and `@autoreleasepool`.
- **Class registration works, across translation units.** `objc_getClass("A")`
  answers `A` in a single-TU link; and a 3-object link with the base class in one
  TU and subclasses in two others registers all three (`REG R=R`, `REG S=S`,
  `REG T=T`), identically to the same classes in one object.

### A RETRACTED false alarm, and what it actually was

An earlier run of this gate **wrongly reported a blocker** ("a multi-object link
registers only ONE module"). The observation was real; the conclusion was not.
Two independent facts were misread as one failure:

1. **The COMDAT groups are BY DESIGN, and must NOT be "fixed".** clang emits each
   TU's module in identically-signed groups (`.group [.objc_init]`,
   `.group [.objc_ctor]`, `.group [.objcv2_load_function]`), so a link keeps
   exactly one of each — a 2-object link really does show `.init_array` = 8 bytes
   and one `.objc_init`. But the module struct **carries no per-TU data**: its 16
   pointers are all to linker-computed section bounds
   (`__start___objc_selectors`/`__stop_…`, `__start___objc_classes`/`__stop_…`,
   plus `class_refs`, `cats`, `protocols`, `protocol_refs`, `class_aliases`,
   `constant_string`). One module therefore describes the WHOLE image, and a
   single `__objc_load(&module)` (`loader.c:170`) registers every TU's classes —
   which is exactly what the linker's dedup is for. The class/cat/selector
   *sections* are deliberately NOT grouped, so they accumulate.
2. **The real symptom was `[[Greeter alloc] init]` returning nil** — a bug in the
   *probe's* minimal root class, not in the runtime: `class_createInstance`
   returns nil when `cls->instance_size < sizeof(Class)` (`runtime.c:360`), and a
   class with **no instance variables** has size **0** (measured:
   `class_getInstanceSize` = 0). Giving the root class an `isa` ivar made every
   marker pass. **Any first-party root class (the Foundation-lite) must declare
   storage for at least the `isa`** — this is a live trap, not a footnote.

With the probe corrected, the gate passes:

```
RUN class=Greeter                     @implementation, via the 2.0 ABI metadata
RUN greet=hello from an ObjC method   an instance method through objc_msgSend
RUN category.doubleCount=42           a CATEGORY (__objc_cats + section symbols)
RUN protocol.conforms=1               a PROTOCOL (__objc_protocols)
RUN property.count=21                 @property/@synthesize + ivar offsets
RUN block=15                          blocks (stack block + Block_copy to heap)
RUN arc.block=12                      ARC (objc_retainBlock / objc_storeStrong)
RUN catch=Greeter                     @try/@throw/@catch (personality + libunwind)
RUN synchronized=ok                   @synchronized
RUN autoreleasepool=ok                @autoreleasepool
RUN DONE
```

A robustness note that stays: `class_getName(objc_getClass(NULL))` **segfaults**
rather than answering null — a trap for any future gate.

Two dead ends, recorded so they are not re-tried: **GNU ld and lld behave
identically** on the groups, and renaming the per-object sections/symbols with
`llvm-objcopy` changes nothing (both consistent with the dedup being intended).
`-fobjc-runtime=gnustep-1.9` is not a viable alternative route either: it emits
no COMDAT groups but calls `__objc_exec_class`, which v2.3 does not export with
`OLDABI_COMPAT=OFF` (verified: undefined reference), so it would cost a rebuild
plus the 2.0 reflection metadata for no gain.

### Method notes (cheap to learn, keep them)

- **An ObjC *executable* must be linked with the C++ driver.** libobjc.so's
  undefined C++ symbols are resolved at *executable*-link time, and
  `tools/musl-clang64.sh` has no libc++ in its link path — the C wrapper produced
  a wall of `undefined reference to _Unwind_*` / `std::*`. P2's wrapper mode must
  therefore be C++-driver-based.
- **Host-running a guest binary**: its interpreter is
  `/System/Libraries/ld-musl-x86_64.so.1`, absent on the host, so a host run needs
  `-Wl,-dynamic-linker,$PWD/.build/musl64/lib/ld-musl-x86_64.so.1` (passed last;
  it overrides the wrapper's own) plus
  `LD_LIBRARY_PATH=.build/musl64/lib:.build/llvm-cxx-prefix/lib:.build/objc-prefix/lib`.
  That is a HOST check of the artefact we intend to ship — not a guest gate.
- **`libobjc.a` is incomplete upstream**: the static target's source list omits
  the OBJCXX sources (`arc.mm`), so ARC symbols are missing from the archive. The
  shared library — the thing we would stage — is unaffected.

## 9. P2/P3 outcome (measured, 2026-09-17) — Objective-C on the guest

### P2: the toolchain seam

- **`tools/musl-clang-objc64.sh`** is the ObjC wrapper. It **delegates to
  `tools/musl-clang++64.sh`** and adds exactly four things:
  `-fobjc-runtime=gnustep-2.0` (not optional — on ELF clang's default is the
  legacy GNU-runtime ABI, which is the GPL runtime's), `-fblocks`, the runtime's
  include dir, and `-lobjc` LAST, on link lines only. It delegates because the
  runtime carries undefined C++ symbols that an *executable* link must resolve
  (§8), and it never adds `-fobjc-arc`: ARC is a per-file choice and mixing is
  normal (a root class *cannot* be ARC).
- **`.m`/`.mm` is taken from the extension. `-x objective-c` must never be
  passed**: the wrappers append crt objects and archives after `"$@"`, and `-x`
  would apply to them too (§8's measured trap).
- **`userland/tests/objc_smoke.{h,m,objc_smoke_support.m}`** is the probe, in
  **two translation units on purpose**: the class and its root class live in
  `objc_smoke_support.m` (MRR), the CATEGORY on that class in `objc_smoke.m`
  (ARC). Cross-TU class registration is the case that was misdiagnosed during P1,
  so it now stays in the acceptance. The header carries the `isa`-storage warning
  (§8's other trap) where the next person will read it.
- The probe is self-checking: one `OBJC-SMOKE <name> ok|FAIL <detail>` line per
  check, an `OBJC-SMOKE RESULT ok=N fail=M` tally, `OBJC-SMOKE DONE`, and a
  non-zero exit if any check failed.

### P3: staging, the build, and the gate

- **`$(OBJC_STAMP)` joined `userland64`'s prerequisites**, exactly where
  `$(LLVM_CXX_STAMP)` already was, so `make rootagfs` builds the runtime and a
  fresh clone fetches it (twice-pinned). That is not new machinery: the C++ stack
  has required a network fetch since `cpp-toolchain-plan.md`.
- `/System/Libraries/libobjc.so.4.6` is staged under the libconfig/libc++ rule
  (the versioned file; the loader resolves the SONAME), and the probe lands at
  `/System/Shared/tests/objc_smoke`.
- **`tests/cases/objc_smoke.py`** (fast tier) asserts every probe check **by
  name**, the probe's own tally, that no FAIL line exists, and the exit status — a
  probe that stops early cannot pass by printing a good-looking tally.
- **Measured on a real guest boot**: `TESTS-OK 1/1 case(s), 6/6 check(s)`, with all
  eleven probe checks `ok` (`class`, `greet`, `origin`, `category.twice`,
  `protocol`, `property`, `block`, `arc.block`, `catch`, `synchronized`, `pool`).
  Objective-C works on the guest, not only on the host as in §8.

### The gate that caught the first attempt

The repo's own **M4 toolchain gate** (`tools/toolchain-gate.py`, which forbids a
bare `gcc` token in `tools/` and the Makefile) failed the build on this wrapper's
*comment* — the sentence explaining that clang's default ABI is the legacy GNU
runtime's carried that token in backticks. Reworded to name the ABI without it.
Worth knowing before writing any `tools/` script that describes compiler ABIs.

### Deliberately NOT here

- **No headers staged for the guest**: the runtime's `include/objc/` stays in
  `.build/objc-prefix`, the same choice the C++ stack makes, so an *on-guest* ObjC
  rebuild is not self-hosting yet — recorded in
  `docs/design/self-hosting-packages.md` §6 with the rest of libobjc2.
- **No Foundation**: libobjc2 is the language and its runtime only; the class
  library is ours to write — that is P4, a separately-gated effort.
- **No `libBlocksRuntime`**: libobjc2 embeds its own, and linking both collides.
