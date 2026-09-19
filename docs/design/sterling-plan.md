# Sterling — a modern syntax over Objective-C, for the Argentum OS (plan)

Status: **DRAFT (2026-09), for review.** Nothing implemented. **The language is
`docs/design/sterling-syntax.md`; this plan is the compiler.** Direction, decided
by the user (2026-09): Sterling is a **surface** over Objective-C — a modern,
Swift-flavored syntax whose every construct is a re-spelling of an Objective-C
concept — transpiled to `.h`/`.m` that the system's own clang then compiles.
Where this plan and the syntax document disagree, **the syntax document wins.**

The substrate this plan needs already exists and is **measured**, not assumed:
the Objective-C runtime (`docs/design/objc-toolchain-plan.md` §8–§9, P0–P3
green), the first-party class library (`docs/design/foundation-plan.md`,
F0–F3 landed), the compiler wrappers (`tools/musl-clang-objc64.sh`), and the
build/harness conventions every gate here reuses. This plan adds **one
artifact the system does not have: a compiler.**

**Not Vala, and not Swift.** No GObject/GType/`GValue`, no GIR, no GLib
prelude — and no new object model either: the runtime is libobjc2 and the class
library is ours, both already built. The licence question a Vala port would
raise (forking an LGPL compiler, or copying its grammar) does not arise: the
surface is ours and the front-end is written from scratch either way.

## 1. What this is, and what it is NOT

**Is:**

- a **transpiler**: `.ag` → `.h` + `.m`, compiled by the system's clang
  against libobjc2 and the Argentum Foundation;
- **a surface with no new semantics.** `docs/design/sterling-syntax.md` §3 is the
  complete concept map, and that map is also the complete scope — every Sterling
  construct is some Objective-C construct, re-spelled. The syntax document's §0
  is the rule that keeps it that way: *a construct that cannot be stated as
  "this ObjC thing, spelled this way" is out of scope*, with exactly one
  admitted exception — a **checked** operation (`x!`), which asserts and removes
  a case rather than adding a capability;
- the second language of the system build, alongside C/C++/ObjC.

**Is NOT:**

- **a new object model or runtime.** The runtime is `libobjc2`
  (`objc-toolchain-plan.md` §3.1) and the class library is the Argentum
  Foundation; both are already gated. The language adds neither;
- **a language with features ObjC lacks.** No operator overloading, no signals
  *as a language feature*, no KVC sugar (§7) — and the generics it does have are
  ObjC's own *lightweight* ones rather than something of its own
  (`sterling-syntax.md` §5, §7.26). This is a design commitment, not a limitation to
  be lifted later;
- **a GUI toolkit** (it drives `docs/design/cocoa-parity-plan.md`'s library),
  **a binding generator** (§7), **a VM**, or **an IL**.

## 2. Why — and the honest trade-off

**A pure surface borrows all its capability from Objective-C, so its ceiling is
Objective-C's.** That is the trade, and it is deliberate: when a feature the
runtime lacks is needed, the change belongs in the runtime, not in the language.
What the surface buys instead:

- **a modern syntax over the concepts we already use.** The specimen
  (`sterling-syntax.md` §1) and its emitted Objective-C (§2) are the pitch;
- **no ecosystem tax.** A language like this normally lives or dies on
  *binding*, through GIR, to libraries it does not own — the whole of Vala's
  value and the whole of its fragility. Sterling's standard library is the
  Argentum Foundation: first-party, MIT, in this tree, gated on a guest boot
  (`TESTS-OK 4/4 case(s), 24/24 check(s)` for F0–F3). No IDL to consume, no
  binding engine to write, no upstream to diverge from;
- **generated code a person can read.** A bug is always chaseable into the
  emitted `.m`; the language is never a black box.

### What already exists vs. what this plan adds

| piece | state | record |
|---|---|---|
| ObjC frontend in clang-19, `.m` compiles for musl | works today | `objc-toolchain-plan.md` §2 |
| `libobjc2` runtime (gnustep-2.0 ABI, ARC, blocks) | built, guest-green | §8–§9 |
| wrapper `tools/musl-clang-objc64.sh` | landed | §9 |
| Foundation core (root class, strings, values, collections) | F0–F3 landed | `foundation-plan.md` |
| **the language surface** | drafted | `sterling-syntax.md` |
| **a Sterling compiler (`sterlingc`)** | **—** | this plan |

## 3. Design decisions

1. **Emit Objective-C source, not IR.** `sterlingc` reads `.ag` and writes
   `.h` + `.m`. The generated code is human-readable — a requirement, not a
   side effect — and it is also the cheap choice: clang-19 is already the system
   compiler, already speaks the 2.0 ABI, and needs no new backend. The front-end
   is ours, written from scratch; there is nothing to fork.

2. **Target `-fobjc-runtime=gnustep-2.0` on libobjc2, ARC by default.** The
   runtime decision is `objc-toolchain-plan.md` §3.1; the ownership decision is
   `foundation-plan.md`'s (*ARC is the house default*). The compiler emits
   ARC-idiomatic code and never a manual `retain`/`release`.

3. **Emit `.m`, not `.mm`, in v0.** Objective-C is a C superset, so C interop
   needs nothing more; `.mm` is the escape hatch if Sterling ever needs the parked
   C++ toolkit, and v0 does not open that door.

4. **The prelude is the Foundation.** The implicit import is
   `Foundation/Foundation.h` plus the generated header set. `NSString`, `NSArray`
   and the rest are the class library we own — no second standard library, and
   **no Sterling library to stage**: the only emitted runtime code is the
   force-unwrap trap of §3.14, which is a **header-only** helper.

5. **The concept map is the scope.** `sterling-syntax.md` §3 lists every construct
   Sterling has, and §7 lists its decisions. The compiler implements exactly that
   map and nothing else. **A construct not in the map is not a missing feature —
   it is out of scope.**

6. **Property and ownership qualifiers are inferred, not written.** A
   class-typed property is `strong`, a scalar/struct property is `assign`; the
   annotations (`(readonly)`, `(weak)`, `(copy)`) are written in ObjC's own place
   — after the keyword (`sterling-syntax.md` §5). The compiler emits the qualifier;
   the source carries no ARC bookkeeping.

7. **`throws` lowers to a trailing `NSError **`, never to `@throw`.**
   Recoverable errors are the `NSError` out-parameter convention
   (`foundation-plan.md` F4); the selector the `throws` parameter gains is
   `sterling-syntax.md` §7.6.

8. **No class prefix by default.** The surface spells classes as written
   (`MyClass`). The only constraint is the runtime's reserved names — `Object`,
   `Protocol`, `ProtocolGCC`, `ProtocolGSv1`, `__IncompleteProtocol`,
   `_NSConcrete*Block` — the same class of collision that forced the Foundation's
   `NS` prefix (`foundation/NSObject.h`). *Open:* a mandatory module prefix
   (`sterling-syntax.md` §7.7).

9. **Extension `.ag`.** ASCII spelling in code and paths; `Sterling` in prose. The two letters are
   Argentum's rather than the language's own, and they name the artefact rather than the source:
   a `.ag` file is Argentum's, like the AGFS image a package ships in. **The Markdown fence tag
   stays `sterling`** — a tag names the *language*, an extension names the *file*, exactly as an
   `objc` fence holds code that lives in a `.m`. Nothing else moves with this: the compiler is
   still `sterlingc`, and the `STERLING_*` macros and `__sterling_*` reserved names keep their
   spellings.

10. **`sterlingc` v0 is written in C++ over libc++**, host-side, a *tool* like
    `tools/mkagfs.py`. This avoids the bootstrap paradox and reuses the C++
    toolchain. **Self-hosting is a later milestone, not a premise** (K6).

11. **The compiler is deterministic.** Same input, byte-identical output, so a
    rebuild is a no-op. See §5 for why "idempotent" is not the same as "correct".

12. **Non-nullable by default (decided, 2026-09).** Every reference-typed
    declaration is non-null unless written `T?`; the generated headers open a
    clang assumed-non-null region (`sterling-syntax.md` §4). This is the one
    decision that is *not* purely a syntax matter — it needs the Foundation's
    headers annotated to hold across the boundary (§6), so it is recorded here
    as a cross-plan dependency as well as in the syntax document.

13. **The optional flows need scoped bindings, not a dataflow pass
    (decided, 2026-09).** `x!` asserts and traps (§3.14 is the trap);
    `if let` / `if var` / `guard let` / `guard var` branch and bind
    (`sterling-syntax.md` §6). The bindings narrow *structurally* — a binding's own
    scope for `if let`, the remainder of the block for `guard let` — so the
    compiler needs scoped bindings, not a general narrowing analysis. That is why
    v0 can have the ergonomics of Swift's optional handling without Swift's
    flow-typing engine.

14. **Force unwrap traps (decided, 2026-09).** `x!` on a `T?` yields the raw
    value and aborts if it is null (`sterling-syntax.md` §6). It is the **only**
    construct with no plain-ObjC spelling (§3.1's rule admits it as a *checked
    operation*), and therefore the only one that emits code ObjC would not: a
    null check. The helper is **header-only** (`static inline` or a
    statement-expression macro), not a library, so §3.4's "no library to stage"
    holds — and it must be an **unconditional** trap: a conditional assert
    (`NSAssert`, or anything an `NDEBUG`/optimised build strips) would silently
    make `!` non-trapping, which is the one thing it exists to prevent.

15. **The ObjC wrapper gains `-fms-extensions -Wno-microsoft-anon-tag`
    (decided, 2026-09).** Struct inheritance lowers to an anonymous tagged member
    (`sterling-syntax.md` §5, §7.16), and the flag **cannot be per-file**: a
    derived struct's definition lives in a generated *header*, so every
    translation unit that includes it needs the flag. It therefore goes in
    `tools/musl-clang-objc64.sh`, beside `-fobjc-runtime=gnustep-2.0` — and the
    suppression rides with it, because clang warns `-Wmicrosoft-anon-tag` on the
    shape we generate on purpose, which a build treating warnings as errors would
    stop on. *Recorded alternative:* a **named** first member (`Point base;`)
    needs no flag and no warning, at the cost of `p3.base.x` in the emitted code
    (measured equivalent layout — the base sits at offset 0 either way).

**Four rules settled in conversation (2026-09), with their implementation state.**
Recorded together because the *document* and the *compiler* can disagree while both
look correct — the failure mode this project has now hit four times.

| rule | stated in | state |
|---|---|---|
| An omitted `-> T` means `-> Void` | `sterling-syntax.md` §5, Method | **enforced** — `parse_func`, and `parse_decl`'s method and operator branches, all default to `Void` |
| Enum members are written `case name` (Swift's spelling) | §5, Enum — 2026-09, superseding the bare names the earlier text used | **enforced** — a bare name is now an error |
| A protocol carries requirements only, and a body written there is an error | §5, Protocol | **enforced** as the body rule lands |
| A claiming type that does not implement every required member is a compile-time error | §7.48 (already written) | **NOT implemented** — needs the checker below |

**The conformance checker is a milestone, not a task.** It cannot be decided from
syntax: it needs a class's conformance list and a protocol's requirements *in the
AST* — today the parser scans and drops both — and then a pass that walks every
claiming type, resolves the protocols it names, and checks each required selector.
That pass is **the compiler's first semantic step.** It is also the reason
`protocol`, `extension` and `category` stop being scanned: their contents *are* the
data the check runs on. A requirement satisfied by a **superclass** counts, since
the selector is what the runtime dispatches on; `optional` members are exempt, and
a send to one with no guard is a warning rather than an error (§7.48).

## 4. Phases (the K-series)

Phases track **coverage of the concept map**, not features of our own — there
are none. Guest gates are *sparing* (`gate-runs-sparingly`); the compiler's
correctness lives in a **host** suite (§5), which is both cheaper and where the
bugs actually are.

### K0 — The surface frozen

- `sterling-syntax.md` §7's decisions answered; §4–§6 (types, declarations,
  expressions) reviewed against the specimen.
- **Verify:** the syntax document's open questions are closed.

### K1 — THE RISK GATE: the specimen compiles

- `tools/sterlingc/` — lexer, parser, AST, emitter — sized to compile exactly the
  specimen (`sterling-syntax.md` §1): one class, one superclass, a stored property,
  a read-only property, a `class method`, a `method`, and a labeled call to a C
  function.
- `tools/sterlingc.sh` — the driver: `.ag` → `.h` + `.m` →
  `tools/musl-clang-objc64.sh` (never `-x`; §6). The emitted `.h`/`.m` are checked
  against `sterling-syntax.md` §2 — that pairing is the spec, including the
  assumed-non-null region (§3.12).
- **Verify:** a host golden check (the emitted source byte-matches the document)
  **and** a guest probe: the compiled class runs under libobjc2, prints one
  `STERLING <name> ok|FAIL <detail>` line per check plus a `STERLING RESULT ok=N
  fail=M` tally, and exits non-zero on failure — the `objc_smoke` pattern
  (`tests/cases/objc_smoke.py`).
- **Status (2026-09): both halves are in.** The host legs are `make
  sterlingc-check` (golden, corpus, reject, compile). The guest half is
  `userland/tests/sterlingc_k1.m` linked with the compiler's own output at build
  time and run by `tests/cases/sterlingc_k1.py`. The driver is hand-written ObjC,
  not Sterling, because the specimen is a class with no top-level statements and
  the emitter cannot yet emit calls or statement bodies — that widening is K2's.
  K1's claim is the *chain*, and this is the leg that exercises it;
  `tools/sterlingc/tests/probe/sterlingc_k1_probe.ag` remains the Sterling
  version, to be swapped in when the emitter can produce it.
- **Green here proves the chain: `.ag` → ObjC → clang → libobjc2 → the
  Foundation, on a guest boot. Do not plan past K1 until it passes.**
- **The specimen is narrower than the language now is (2026-09).** It exercises no subscript, no custom
  operator, no `defer`, no `with`, no `where`, no range pattern and no scalar optional — all of which
  entered `sterling-syntax.md` after this gate was written. K1 stays as it is, because its job is the
  *chain* rather than the coverage; but each later phase should add a specimen for the constructs it
  introduces, and none of them should be called done on K1's strength.

### K2 — The class surface and the optional flows

- Protocols, categories/extensions, conformance lists, the full property forms
  (get/set, the `(readonly)` annotation, `(weak)`/`(copy)`),
  `Self`/`instancetype`, `super`; `let`/`var` locals — including a type supplied
  by the initializer rather than written (`sterling-syntax.md` §5, §7.19).
- **The optional flows** (`sterling-syntax.md` §6, §7.10): `x!` and its trap
  (§3.14), the four binding forms and the emitter's rename map (§6), the
  `guard`-else-must-exit diagnostic, and `nil` in a non-null position.
- **Verify:** host goldens plus a `sterling_model` guest probe; the compiler's
  **diagnostics** are checked too (an ill-typed program must fail *at compile
  time*, by name). Two behaviours have to be *seen*, not assumed: a probe
  unwraps a nil `T?` and the trap fires (§3.14 — the one piece of source-emitted
  runtime behaviour), and a `guard let` on a nil value leaves the scope without
  running the rest of the block.

### K3 — Types, structs, imports, and C interop

- The type table (`sterling-syntax.md` §4) and its **reference/value rule** — a
  class type is a reference, a scalar and a **struct** are values; declared
  structs (`struct`), imported ones (`NSRange` and friends), struct literals (a
  C compound literal), **struct methods** (the function lowering, the `mutating`
  / `const` receiver, the by-address call site), and **struct inheritance** —
  the anonymous member at offset 0, the upcast on an inherited call, and the
  `-fms-extensions` contract behind it (§3.15) (`sterling-syntax.md` §5–§6);
  `import` and the generated `@class` forward declarations and `#import`
  ordering; calling imported C functions with checked-and-dropped labels.
- **Nullability at the boundary**: an unannotated import is *unknown*, not
  non-null (§6), so K3 has to decide the bridging rule
  (`sterling-syntax.md` §9.5) and emit the assumed-non-null region of §3.12.
- **Verify:** a case that imports a real Foundation header and calls into it,
  and a struct-inheritance case that proves the *upcast* (a `Derived` used
  through a `Base *` and an inherited method called on it) rather than merely
  that the object compiled.

### K4 — Blocks, ARC, and errors

- Closures → blocks; ARC capture; the property annotations (§3.6);
  `throws` → `NSError **`; `autoreleasepool` / `synchronized` / `try`.
- **Verify:** a probe covering a block captured by an object, a `weak` reference
  going nil, and an `NSError` round trip.

### K5 — The remaining map

- Literals (`[]` / `[:]` / `#selector`), the two **enum** kinds — the plain C
  enum and the **tagged union** for associated values, including the qualified
  access (`E.valueOne` / `.valueOne`, §7.18) and the proposed matching form
  (`sterling-syntax.md` §5–§6, §9.13) — and `AnyObject` dynamism
  (`sterling-syntax.md` §9.3).
- **Verify:** one check per map row added; the map is the checklist.

### K6 — Self-hosting entry and packaging

- A `docs/design/self-hosting-packages.md` §6 entry for `sterlingc` (standing
  policy): its build-time requirement is a C++ compiler the system already has.
- Decide how Sterling **programs** ship: `.ag` compiled at build time, or
  generated `.h`/`.m` committed (§6's dependency trap).

### K7 — Documentation

- The language reference rides with each construct (the
  `uikit-documentation-plan` rule), behind a coverage gate in the
  `tools/uikitdoc.py` spirit.

## 5. Verification and gates

- **Host suite first.** `tools/sterlingc-tests`: a corpus of `.ag` inputs with
  golden `.h`/`.m` outputs, host-run probes, and diagnostic checks. This is where
  the compiler is tested; it needs no QEMU.
- **Guest gates are integration only** — `tests/cases/sterling_*.py`, fast tier,
  asserting every probe check **by name**, the probe's own tally, absence of
  `FAIL` lines, and the exit status (`objc_smoke.py`'s contract verbatim).
- **`tools/sterling-gate.py`** (the `toolchain-gate.py` shape): emitted code is
  ARC-clean (no `retain`/`release`/`dealloc`); the emitter is deterministic;
  **and** the output is *correct* — which is not the same check (`weaver-ib0`:
  *idempotence alone is not a valid check*).
- **Do not add a token that trips `toolchain-gate.py`** — it forbids a bare
  compiler-name token in `tools/` and the `Makefile`, and it fired on a
  *comment* once (`objc-toolchain-plan.md` §9).

## 6. Risks / gotchas

- **The front-end is the entire cost.** There is no permissive C#-ish front-end
  to take; the parser, the type checks and the emitter are ours. K1 is sized to
  find out whether a contained effort is enough, and is the only phase that can
  kill the plan.
- **`!` is the first thing the surface has to *emit* rather than re-spell.** The
  rest of the concept map is a 1:1 rename; the force-unwrap trap (§3.14) does not
  exist in ObjC, so the language now owns a sliver of runtime behaviour. Two ways
  it goes wrong: the trap gets compiled out by an optimised build, or it is
  factored into a staged library and quietly breaks §3.4. Keep it header-only,
  make the trap unconditional, and prove it fires on a nil unwrap (K2's probe).
- **The binding forms make the emitter stateful, and C is why.** The four binding
  forms cannot emit a fresh declaration named after the binding when the binding
  shadows its own source (`guard let x = x`): C forbids redeclaring a name in one
  scope, and `let` is immutable, so the emitter must give the value a fresh C
  name and carry a **rename map** for the rest of the scope
  (`sterling-syntax.md` §6). The temps need a per-block counter, or two bindings in
  one block collide. Nothing here is hard — but it is the first part of the
  emitter that is not a straight tree walk, so size it into K2 rather than
  discovering it there.
- **`let`'s `const` goes in a different place for class types, and the naïve
  emission is wrong.** `let n: Int32 = 3` is `const int32_t n = 3;`, but `let s:
  NSMutableString = …` is `NSMutableString * const s`, **not** `const
  NSMutableString *s` — the pointer is const and the object is not, or the first
  mutating message is a "discards qualifiers" error. The same applies to a
  binding temp in the binding forms, and a struct follows the scalar rule
  (`const Point p`, which is what makes a mutating method call on a `let` an
  error — `sterling-syntax.md` §5–§6). One line in the type emitter, invisible
  until it is not — and `var` is the same code path with the `const` dropped, so
  the bug is in the shared helper.
- **`-fms-extensions` is a dialect change for the whole ObjC build, not a local
  one.** The shape it enables — a derived struct's anonymous base member —
  lives in a generated **header**, so it cannot be a per-file flag: every
  translation unit that includes a Sterling-generated header needs it (§3.15),
  including the Foundation's own. That makes the feature's real cost the dialect,
  not the syntax, and K3's verify must show the existing ObjC still builds clean
  with the flag on — or the named-first-member alternative (also measured, no
  flag) becomes the better trade.
- **The non-null rule is only as strong as the annotations, and nothing is
  annotated yet.** The surface makes every reference type non-nullable by default
  (`sterling-syntax.md` §4), but a default is a *declaration* promise: an imported
  declaration is known non-null only if its header says so. Measured:
  `userland/foundation/` contains **no** `_Nonnull`/`_Nullable`/
  `NS_ASSUME_NONNULL`, and neither do libobjc2's headers — so today the rule
  holds inside Sterling-written code and has a hole at every import. Annotating the
  Foundation is a `foundation-plan.md` item and the honest fix
  (`sterling-syntax.md` §9.5); the alternative is a rule that is a lie at the
  boundary.
- **`NS_ASSUME_NONNULL_BEGIN` does not exist here.** The emitter must not depend
  on it: use clang's own `_Pragma("clang assume_nonnull begin")`/`end`. (Adding
  the macro pair to the Foundation is optional sugar, not a requirement.)
- **The language cannot outrun the Foundation.** `throws` needs `NSError` (F4);
  the collections need F0–F3 (landed). Sterling's schedule is *bounded below* by
  `foundation-plan.md`'s; plan them together.
- **A generated root class needs the runtime's root-class attribute.**
  `__attribute__((objc_root_class))` is load-bearing: without it clang does not
  emit `@"…"` as an object, and message sends to constant strings fault
  (measured, `foundation/NSObject.h`). The emitter adds it to any class with no
  superclass.
- **Generated headers and `make` dependencies — the build trap, library
  edition.** `make rootagfs` does **not** rebuild consumers on a header change; a
  stale generated `.h` links and silently runs the old path. Wire the generated
  headers into the Makefile's `.d` (from `-MMD`) dependencies, or a refactor
  looks like a no-op.
- **`-x objective-c` must never be passed** — the wrappers append crt objects and
  archives after `"$@"`, and `-x` applies to them too (§9). Emit `.m`; rely on
  the extension.
- **Executable links go through the C++ driver.** `libobjc.so`'s undefined C++
  symbols resolve at *executable*-link time, which is why
  `musl-clang-objc64.sh` delegates to `musl-clang++64.sh`; `sterlingc.sh` must call
  that wrapper.
- **No `libBlocksRuntime`** — libobjc2 embeds its own; linking both collides
  (`objc-toolchain-plan.md` §3.4).
- **`weak` needs runtime support.** `__weak` requires libobjc2's
  `objc_storeWeak`/`objc_loadWeak` paths; confirm before K4 promises it.
- **The name.** `Sterling` is a *package* collision (a JS mocking library, a Rust
  GUI prototype), never a *language* one — fine for an OS language, unusable in a
  registry. Cite the bird's origin in the docs; it costs nothing.

## 7. Non-goals (v0)

No operator overloading, macros, or reflection beyond the runtime's. Generics
exist only as ObjC's lightweight ones, and only on a class
(`sterling-syntax.md` §5, §7.26).
No signals/notifications as language features (`NSNotification` and blocks are
callable — that is the whole of what is needed). No dispatch on a struct —
inheritance there is layout plus upcasting, and protocol conformance stays out
(`sterling-syntax.md` §5, §8). No new runtime, no VM/IL, no JIT. No Apple runtime.
No PIE mains. No FFI/binding generator — C and ObjC are imported from headers; a
clang-AST importer (the sane replacement for GIR) is a **later, separate** idea,
LLVM-family and therefore doctrine-compatible, but not this plan. No GUI.

## 8. Open questions

1. **The syntax document's §7/§9** — selector composition first (§7.1); it fixes
   the shape of every declaration and every call. Then the ones the non-null rule
   opened: required vs inferred types on locals (§7.12), whether the compiler
   *verifies* non-null (§7.11), sending to an optional (§9.6), comma-separated
   bindings (§9.7), and overriding on a struct (§9.12).
2. **The boundary rule for unannotated imports** (`sterling-syntax.md` §9.5), and
   whether annotating the Foundation is scheduled here or in
   `foundation-plan.md`.
3. **The `-fms-extensions` placement** (§3.15): wrapper-global, or the
   named-first-member alternative that avoids the dialect change. Decide before
   K3.
4. **The front-end's own shape.** Hand-written recursive descent, or a parser
   generator (the doctrine's permissive picks: `byacc`, `ragel`)? The compiler's
   language is C++ over libc++; it is not required to be host-free.
5. **Packaging (K6):** are generated `.h`/`.m` committed, or produced at build
   time?
6. **Module/header granularity** (`sterling-syntax.md` §7.9): one header per class,
   per module, or per program?

## 9. Milestones

- **M1 (K0+K1):** the surface is frozen and the specimen compiles and runs on a
  guest. **The feasibility question is answered — the only gate that can kill the
  plan.**
- **M2 (K2+K3):** the class surface, the optional flows, the types, structs,
  imports and C interop.
- **M3 (K4):** blocks, ARC and errors — the language can express the Foundation's
  own idioms.
- **M4 (K5):** the remaining concept map.
- **M5 (K6+K7):** the self-hosting entry and the documentation gate.

### Sizing, and what dominates it

The K-series above is already sequenced correctly; this adds what it costs and where the risk sits.

- **Why the total is small for a compiler: there is no back end.** No codegen, no optimiser, no
  instruction selection, no runtime, no standard library — the output is `.h` and `.m` *text*, and
  clang, libobjc2 and the Foundation (F0-F4, already done) do everything after. The job is
  lexer → parser → checker → emitter and nothing else, which is the whole reason a hand-written
  toolchain is viable here.
- **Rough line counts, in C:** lexer ~1k; parser and AST ~3-4k, where selector composition (§7.1) is
  the hardest rule in the front end; checking ~3-5k (types and inference, the conversion table,
  nullability, `guard`-must-exit, definite initialization, the promoted diagnostics); emission plus
  monomorphisation ~4-6k; tests, probes and corpus roughly as much again. **12-19k in total.**
- **The emitter surfaces added *after* this plan was drafted (2026-09), which the first estimate did
  not count.** Each is a rewrite with rules of its own, and each belongs in the K-phase that owns its
  area rather than in a phase of its own:
  - **Custom operators** (§7.72, §7.73) — precedence resolved from the symbol table plus the unary /
    binary split, arity and side taken from the parameter names, mangling that maps `.`, `\` and `/`
    to identifier characters, and the member-only `[]` / `[]=` pair emitted as ObjC's two subscript
    selectors. This is the largest single addition.
  - **`defer`** (§7.71) — every exit path carries the enclosing scopes' deferred calls, innermost
    first, so the cost is code size where a scope has many exits.
  - **`with`** (§7.70) — a scope rewrite in name resolution, admissible only because the target's type
    is static.
  - **`where`** (§7.73) on `case` and on `for` — an if-chain lowering forced on any `switch` carrying
    one, since a C `case` label is a constant.
  - **Scalar optionals** (§7.62, pair-structs) and the **sized and multidimensional array forms**
    (§7.64: `[N]`, brace lists, zero-fill) — neither counted in the original estimate.
- **Rough calendar**, one experienced person full time: **2-3 months** to M1 — parser and emitter, no
  checks, emitting compilable Objective-C; **+2 months** for the checker; **+2-3 months** for
  monomorphisation, the emitter surfaces above and the header importer; **+1 month** for the build
  integration and for cleaning up the emitted output. **5-7 months to something that compiles real
  programs.**
- **The three unknowns, in order of size — none of them the parser:**
  1. **The header importer**, the largest technical risk. Every rule about *imported* declarations
     rests on it — §4's nullability, §7.31's name-is-the-class, struct field access — and it has never
     been measured. Cheapest route is clang's own AST (dump or libclang), which the doctrine permits.
  2. **The build wiring for modules — diagnosed, and no longer a risk (2026-09).** §7.9's *"could not
     build module"* was never about modules: `Foundation.h` imports its siblings as `<foundation/…>`,
     so a modular build of it needs the same `-I` its importers need. With `-Iuserland` the map builds
     and the probe exits 0. What is left is wiring the map, `-fmodules` and that `-I` into the build —
     small, and no longer an unknown.
  3. **The quality of the emitted code.** "Reads like hand-written ObjC" is a goal rather than a test,
     and §7.63's generics-in-headers pushes against it directly.
- **Monomorphisation (§7.63) is bounded**, worth stating because it is the newest subsystem: a class
  takes object arguments and erases, so only structs, enums and arrays instantiate — no generated
  class registry and no dynamic dispatch.
- **M1 already is the right first move.** The specimen compiled by hand, then emitted by a parser, is
  what buys the most de-risking per week: it validates every emission decision while the checker does
  not yet exist, and it gives the parser a concrete target instead of a plausible one.
