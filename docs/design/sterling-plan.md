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
    (decided 2026-09; *confirmed by the user* when K3 was opened, so the
    alternative below is now closed, not parked).** Struct inheritance lowers to
    an anonymous tagged member (`sterling-syntax.md` §5, §7.16), and the flag
    **cannot be per-file**: a derived struct's definition lives in a generated
    *header*, so every translation unit that includes it needs the flag. It
    therefore goes in `tools/musl-clang-objc64.sh`, beside
    `-fobjc-runtime=gnustep-2.0` — and the suppression rides with it, because clang
    warns `-Wmicrosoft-anon-tag` on the shape we generate on purpose, which a build
    treating warnings as errors would stop on.
    - **The alternative, and why it lost.** A **named** first member
      (`Point base;`) needs no flag and no warning, at the cost of `p3.base.x` in
      the emitted code and a cast at every upcast site (measured equivalent layout
      — the base sits at offset 0 either way). It was the smaller *change*; it was
      not chosen, and the cost is now a standing one: **the ObjC dialect of the
      whole userland is Microsoft-extended**, including the Foundation's own
      sources. K3's verify therefore carries the proof obligation §6 records —
      the existing ObjC must still build clean with the flag on. **That is not
      proven yet**; it lands with structs, not with this slice.

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
- **Landed (2026-09): `x!` and its trap — the language's only runtime behaviour.**
  §6 leaves the emitted shape "provisional; K1/K2 settles it", and this settles
  it: the trap is **`__builtin_trap()`** — one instruction, unconditional, and it
  needs no header at all, which is what "header-only" has to mean when the
  generated header is the only thing a hand-written `.m` may include. §6 forbids
  the alternative by name — an `NDEBUG`-stripped assert "would silently make `!`
  non-trapping in an optimised build" — and the firing is MEASURED rather than
  assumed: the emitted macro, cut out of a generated header and compiled on its
  own, survives a non-nil operand and dies with **SIGILL** on a nil one.
  - Emitted **only where the unit unwraps something**, and the specimens pin that
    from both sides: `EmitsUnwrap` carries the macro block and the other eight do
    not. That is not tidiness — §2's specimen is a byte-for-byte golden, so a
    macro block in every header would change the one output the language is
    specified by.
  - §6's precedence falls out rather than being arranged: the send's receiver goes
    through the unwrap case, so `x!.foo` emits `[STERLING_UNWRAP(x) foo]`, the
    macro being a parenthesised statement expression that needs no extra parens.
  - Still owed from §6: the four binding forms and the emitter's rename map, the
    `guard`-else-must-exit diagnostic, and **`x!` on a non-optional being an
    error** — which needs the operand's type, the same wall the nullable-scalar
    refusals sit behind. `tests/refuse/force-unwrap.ag` is retired rather than
    repaired: its expectation is now the thing that is emitted.
- **Landed so far (2026-09): the statement and expression core.** The emitter was
  a specimen-shaped special case — it wrote `return` and one expression shape and
  *skipped* every statement it could not represent, so a program compiled to
  source with statements silently missing. It is a real tree walk now:
  `let`/`var` locals with §5's type-dependent `const` (a scalar is
  `const int32_t n`, a class type `NSString * const n`), assignment, message
  sends (`o.sel(label: arg)` → `[o sel:arg]`), member access, the binary and
  unary operators with §7.22's parentheses restored from the one precedence table
  the parser also uses, and `if`/`else if`/`else`/`while`. §4's literal default
  and expected type are honoured (`let n: Float32 = 0.1` → `0.1f`, `let d: Float64
  = 0.1` → `0.1`). Everything else **stops, by name** — `tests/refuse/` is the
  leg that holds that, and it exists because two of the old behaviours were worse
  than omission: a closure emitted `nil` and `x!` emitted its operand with the
  trap left out, and both compiled.
  - Measured against the corpus: **13 of 13 parse, 0 of 13 emit.** The gap is the
    map of what K2 has left, not a fault — the corpus's files are the surface's
    constructs, and the emitter refuses the ones it has no rule for. The count was
    2 when this was written and is 0 now, and the two that went are worth knowing
    because one of them was a FALSE PASS: a 0-class program emitted nothing and
    exited 0, and `10-literals-and-types.ag` was emitting an inverted-nullability
    header. The corpus's current blockers, in its own words: **a struct (3 files),
    an enum (3), a nullable scalar or an unknown nullable name (§7.62/§4, 4), a
    stored property's default (§9.16, 1), `defer` (1), generic parameters (1)** —
    which is the priority order for what K2 and K3 have left, read off the corpus
    rather than guessed.
  - **An unresolved document conflict is now load-bearing.** §2's specimen emits
    `baz:(BOOL)arg1 arg2:(NSString *)arg2` from `baz(arg1: Bool, arg2: String)`,
    and §3's map gives `o.foobar(argname: 1, arg2: true)` → `[o foobar:1 arg2:YES]`
    — both say the FIRST selector piece is the method name. §5 says otherwise: it
    gives `class method foo(bar baz: Type)` as emitting `+ (void)fooBar:(Type)baz`,
    i.e. the first parameter's external name attached. The two cannot both hold.
    §2 is the golden, so §2 wins and the emitter follows it for calls and
    declarations alike; a two-name first parameter is therefore emitted the §2
    way, and the disagreement is recorded here rather than resolved in code.
    `sterling-syntax.md` §2 or §5 has to change before that form can be trusted.
- **Landed next (2026-09): the class surface's declarations.** Protocols
  (`@protocol P <A, B>`, with §7.48's members sorted into a required run and an
  `@optional` one — an all-required protocol, most of them, gets no marker at
  all), conformance lists (`@interface X : Y <P, Q>`), and §7.52's ownership
  attribute: one per property, written (`copy`, `weak`) or inferred (§5: a class
  type is `strong`, a scalar is `assign`), with a *computed* property carrying
  none at all. §7.45's forward declarations are emitted for every protocol name
  the program *references*, so the source order of a protocol and its inheritors
  stops mattering.
  - Three silent losses came out with it, all of the same kind: the parser's
    modifier loop consumed `strong`/`weak`/`unowned`/`assign`/`copy` and kept
    only `optional`, so `weak property x: Foo` emitted `(nonatomic, assign)`;
    the property emission hardcoded `(nonatomic, assign)`, so every *class-typed*
    property came out `assign`; and a property's `= value` parsed into a variable
    named `discard`. A fourth: `map_type("Object")` returned `NSObject` with no
    pointer, so a property of type `Object` was declared by value.
  - **Categories and extensions are EMITTED (2026-09), and §7.4's two words are
    two constructs as it argued.** `extension X { … }` is the class extension
    (`@interface X ()`: unnamed, storage allowed, only where the unit defines
    `X`); `category X (Name) { … }` is the category (`@interface X (Name)`:
    named, no storage, the one usable on a class the unit does not own). The
    emission is decided by measurement, not by symmetry:
    **`@implementation X ()` is not legal ObjC** — clang says `expected
    identifier` at the `)` — so a class extension emits its declarations into
    `@interface X ()` and its **bodies into the class's own
    `@implementation X`**. Proven by symbol: the extension's method comes out
    `_i_X__method` (in `X`) where a category's is `_i_X_Name_method`, and the
    extension's *stored* property gets a real ivar and accessors
    (`_i_X__spare`, `_i_X__setSpare_`) — which is exactly the storage a category
    may not have. Two refusals come with it, both because clang either refuses
    for a reason of its own or does not check at all: a **stored property in a
    category** (clang: "instance variables may not be placed in categories") and
    an **extension for a class this unit does not define** (measured: the same
    ivar in a unit that does not implement the class compiles *clean* in clang,
    so nothing checks it — and the consequence is an ABI one). The bodies also
    have to join the class's §7.42 call resolution and its `extern` collection,
    or a call of the class's own method written in an extension would be emitted
    as a C function that does not exist.
  - `parse_extension` read that whole declaration block — the target, the
    category's name, the conformance list, every member — into locals that died
    at the closing brace, and kept a count.
  - **One `.h`/`.m` pair per source FILE, holding everything that file declares.**
    A `.ag` file may declare several classes — ordinary Sterling, and one
    translation unit — so the pair is named after the input file the way a C
    compiler names its output, and the built-in specimen falls back to its single
    class's name (§2's `MyClass.h` from `MyClass.ag`). §7.9's
    per-class/per-module/per-program question is about *modules*, which do not
    exist; inside one file there is nothing to decide. The emitter's predecessor
    wrote one pair per class AND put every class in each of them, so two classes
    produced two identical headers that cannot both be imported — and its banner,
    `#import` and §3.12 assumed-non-null region were per class rather than per
    file. A file with no class at all is refused: there is nothing to name the
    pair after.
  - **§4's `T?` is REFUSED, and that is the only correct option today.** The `?`
    was consumed and dropped, and dropping it was not neutral: the header is
    wrapped in `_Pragma("clang assume_nonnull begin")`, so `String?` emitted
    `NSString *` *inside* that region — asserting non-null, the opposite of what
    was written. Recorded on the type (§7.62's scalar `?` is a different
    mechanism, the pair-struct) and refused until §4's table emits `_Nullable`.
  - **§7.49's initializer, DECLARATION side (2026-09).** A method named `init` is
    an initializer — the name is the whole convention — so its emitted return is
    `instancetype`, and the override is UNCONDITIONAL because §5 says a
    spelled-out `method` and `-> Self` are "accepted and ignored". Its body ends
    in `return self;` whatever the author wrote, also appended unconditionally,
    which is what makes an explicit `return self` the same statement rather than
    a special case. **Both rules were missing and both failed SILENTLY**: the
    emitted `- (void)init:(int32_t)n` is in the `init` family, so clang accepted
    it with only a warning, and the `- (instancetype)init(n:)` that every
    `Class(n: 3)` call is a send to did not exist at all. Reading the emitted text
    was the only thing that could have found it, which is what the `EmitsInit`
    specimen is for.
  - **§7.49's initializer, CALL side (2026-09): the construction and the chain.**
    `T(value: 3)` is a CLASS-receiver send and not a C call, so it emits
    `[[T alloc] init:3]`; and an initializer's chain `self = Superclass()` emits
    `self = [super init:3]` — never a fresh `alloc`, which would discard the object
    being initialized. One tree REWRITE over the whole program, run before the
    `extern` collection, and that placement is what fixes the second wrong answer
    at the same time: `self = Shape(3)` used to emit `self = Shape(3);` **and**
    `extern void Shape(BOOL arg);` — an assignment from a C function that does not
    exist, with a declaration the compiler invented for it — because the
    collection consults node KIND, and it stopped doing that the moment the node
    was no longer a CALL.
    - The pieces are a SEND's, so the call and the declaration agree **by
      construction** rather than by coincidence: `init(sides: Int32)` declares
      `init:(int32_t)sides`, so the call has to write `init:` and not a piece the
      declaration never had. `sterlingc-compile.sh` proves the pair fits — which
      is the check that matters while the §2-vs-§5 selector-piece question above
      is still open.
    - The chain's receiver is CHECKED against the class's superclass rather than
      assumed: the emitted `[super init]` is a send to whatever `super` is, so a
      receiver naming some other class would quietly call a different initializer
      than the one written. It is also the one place `self` is writable, and the
      rewrite only applies inside an initializer.
    - Still owed from the section: a subclass initializer that does NOT chain,
      which §7.49 calls "the thing the rule exists to catch", is not yet a
      diagnostic; and neither is "a declared initializer is required — `Sub()`
      resolves no initializer and is an error". Both need the named class's own
      decls, which the pass already has in hand.
  - Still owed here: `get`/`set` blocks (§7.54/§7.55, so a `set` block and its
    implicit writable `newValue`), §9.16's synthesised *defaults* method (which is
    what a property initializer needs — neither an ivar nor a C struct member may
    carry one), `unowned` (§7.53), and generic parameters (§7.63).
  - **Landed next (2026-09): the spellings a class body needs.** §4's `T?` on a
    class type is `_Nullable` after the pointer — which the header's
    `assume_nonnull begin` region is exactly why it has to be written at all —
    built by one `type_text` so the qualifier lands in the right place in a
    property, a parameter, a return and a local (where the `const` follows it).
    §4's `Self` is `instancetype`. §3's `super` is a receiver like `self` and
    needed a node of its own: it is a KEYWORD, so `super.foo()` was not merely
    unemitted but unspellable — a parse error. And **§7.42's call resolution**:
    an unqualified call naming one of *this* class's methods becomes a message to
    `self` (`print()` → `[self print]`); the resolvable half only, because an
    inherited one needs the superclass's declarations, and that is §9.5's header
    importer. An inherited method called this way still falls through to the
    C-call path, which declares no `extern` for a name it cannot see and
    therefore fails at clang rather than quietly calling something else.
  - Two of these still refuse, and the reason is the same §4 table: a nullable
    SCALAR is §7.62's pair-struct rather than a qualifier, and a name §4's table
    does not cover cannot be classified at all — `Owner?` could be either.
  - **Nested types: `Foo.Bar` in Sterling, `Foo_Bar` in Objective-C (2026-09, a
    decision recorded here rather than in `sterling-syntax.md`, which is silent).**
    A type declared inside another is *written* dotted — `class Inner` inside
    `Outer` is the type `Outer.Inner` — and Objective-C has no nesting, so it is
    emitted as a class of its own under a mangled name. **The dot is the whole of
    the mangling**, which is why it can live in the emitter as a lexical rule:
    `map_type` stays a pure name-to-name function with no symbol table behind it,
    so a reference resolves the same whether the type is in this file, in another
    one, or imported. The cost, stated rather than discovered: a top-level class
    literally named `Outer_Inner` collides with `Inner` nested in `Outer`.
    - Nested **classes** are emitted, before the class that contains them, so a
      property of one is a complete type rather than a forward-declared pointer.
    - Nested **structs and enums** are not emittable at all — neither kind has an
      emission anywhere yet — so they are counted on the outer class and the
      emitter refuses it BY NAME.
    - **A locally declared class is a REFERENCE, and §4's table cannot say so**:
      that table's class rows are the prelude's. So the set of names this file
      declares is what answers "is this a class?", and it decides both the pointer
      (`Outer_Inner *item`, not `Outer_Inner item`) and §5's `const` placement and
      §7.52's ownership inference. An *imported* name still gets neither — §3's
      `property x: Foo` emits a by-value `Foo item;` and therefore fails at clang,
      which is loud, and §9.5's header importer is what will make it right.
    - **§3's `@class` forward declarations are emitted** — one per class this file
      declares that a declaration here names as a type — because a nested type is
      emitted after its outer class and two classes may name each other. `@class`
      is all a POINTER needs; a superclass needs the whole `@interface`, so a
      subclass still has to follow its superclass in the source.
  - **`copy` is a reserved word**, being one of §5's attribute keywords, so a
    local or method cannot be *named* `copy`. That is the language's consequence
    and not a compiler bug, but it is worth knowing before reaching for the name.

### K3 — Types, structs, imports, and C interop

**Landed (2026-09): K3's first slice — §4's `?` in all three of its shapes, and
two silent losses the slice exposed.** The corpus is the priority order read off
the files themselves (13 parse, 1 emits now), and the largest measured group was
the nullable one (4 of 13). What landed:

- **`T?` is three mechanisms and the emitter now picks between them.** A class
  takes `_Nullable` after the pointer; a **value scalar** (`Int32`, `Bool`,
  `Float64`, …, the numeric scalars and `char`) takes §7.62's synthesised
  pair-struct; and a name §4's table does not cover still refuses — §9.5's header
  importer is what will classify an *imported* one, so guessing would pick the
  wrong mechanism on purpose.
- **`SterlingOptional_<t>`** is the pair-struct's spelling — §7.62 fixes the
  FIELDS (`value`, `hasValue`) and not the type's name, so the name is the
  emitter's, recorded here rather than invented per file. One typedef per scalar
  the unit *uses*, emitted before the assumed-non-null region for the trap
  macro's reason, and not at all when the unit uses none (§2's specimen is a
  byte-for-byte golden). The collection walks declarations and parameters at
  every nesting depth — **a type written inside a method BODY is not reached**,
  so a *local* scalar optional names an undefined struct and fails at clang
  rather than silently. That closes with §7.62's binding forms, which is where a
  local's scalar optional actually appears.
- **`CString?` is the boundary case, and it found a real bug.** A pointer is
  nullable natively, so its `?` is §4's qualifier and NOT a struct wrapping a
  pointer in a has-value flag. Getting that far required splitting one question
  into two: `type_is_class`'s `*` test called **every** C pointer a class, so
  §7.52's inference emitted `@property (nonatomic, strong) const char * _Nullable
  cname;` — a header clang *rejects*, generated by this compiler, and caught by
  `EmitsOptional` the first time a specimen named a pointer. "Does this take
  `_Nullable`?" (`type_takes_nullable`) and "is this an object type?"
  (`type_is_class`) are now separate predicates.
- **Two silent losses were being *passed through*, and both now refuse by name.**
  Both were found by measuring a corpus file, not by reading code:
  1. **A lightweight-generic type (§7.63).** `Array<Float32?>` emitted the bare
     identifier `Array` — losing the argument, the fact that it had one, and the
     element type. The check has to sit ahead of the nullable path, because the
     *outer* type of `Array<Float32?>` is not itself nullable.
  2. **A sized declaration (§7.64).** `property slots[8]: Float32` emitted a bare
     scalar: the parser read each bracket's size into an expression and let it die,
     so the storage — the whole of what the declaration said — was never declared.
     The brackets are now COUNTED on the declaration (`st_decl.array_rank`) so the
     refusal has something to be about; §7.64's emission needs the expressions.
- **One specimen had been passing on top of a silent loss.**
  `tests/refuse/for-in.ag` iterated an `Array<String>`, so it passed because the
  generic argument was being dropped and the traversal reached the loop body.
  With the generic refused, the `--refuse` leg reported the refusal for the WRONG
  reason — which is exactly what that leg exists for. The specimen now iterates an
  `Object`: one specimen, one construct.
- `tests/refuse/nullable-scalar.ag` is **RETIRED** rather than repaired — its
  expectation is now the thing that is emitted, the `force-unwrap.ag` precedent.
  `generic-argument.ag` and `sized-array.ag` take its place, and the new golden is
  `tests/golden/EmitsOptional.{ag,h,m}`. Measured: `GOLDEN-SPECIMENS-OK all 10
  specimens match byte-for-byte`, `REFUSE-OK all 22 refused, each by name`,
  `COMPILE-OK` on all ten including `EmitsOptional` (so the pair-struct header
  compiles under `-fobjc-arc` against libobjc2).
- **What the corpus does NOT do yet, which is the next slice:** `10-literals-and-types`
  emits (the first of the 13); `05` moved from `Owner?` to §9.16's property default;
  `04` now stops at the generic type; `12` at `if-binding` — and `if-binding` is
  §7.62's own other half (`hasValue` tests, `x == nil`, `??`), so the pair-struct is
  declared but not yet *read*.

**Landed (2026-09): §7.16's RISK GATE, PASSED — and the struct declaration
RECORDED in the tree.** Two separate things, because the flag is a
whole-userland decision and it had to be measured before any of it was built:

- **`-fms-extensions -Wno-microsoft-anon-tag` is now in
  `tools/musl-clang-objc64.sh`, and the obligation §3.15/§6 records is
  DISCHARGED for the Foundation.** A from-scratch rebuild of **all 197
  Foundation objects** is clean (`MAKE-RC=0`; the only diagnostic anywhere is the
  pre-existing `argument unused during compilation: '-nostdinc++'` note from the
  C++ wrapper, which has nothing to do with this flag), and an A/B compile of
  `NSObject.m`, `NSString.m`, `NSArray.m` and `NSInvocation.m` **with and without
  the flag gives byte-identical diagnostics** — so the flag is *inert* for
  existing ObjC rather than merely tolerated. That was the condition the
  named-first-member alternative was held against, and it is met. What is NOT
  covered by it: the rest of the userland's ObjC (the probes, and anything else
  compiled through the wrapper). The flag is on the wrapper, so they inherit it;
  the full `userland64` build is the wider check and it is not claimed here.
- **`struct` is RECORDED.** `parse_struct` reads the name, the base and every
  member into an `st_struct` (AST) that the program now holds, growing the array
  lazily the way §7.4's extensions do. The members were already *parsed* — a
  nonsense member inside a struct was refused by the front end — so what changes
  is that the declaration survives the parse instead of being counted, which is
  the precondition for emitting it. A **nested** struct is still only counted, on
  the class that contains it, and still refused by name: a nested value type's
  emission is a different question and this one has no answer for it.
  A struct's conformance list is consumed and dropped, which §8 already says
  happens — protocol conformance on a value type is *out of scope*, not
  unemitted — so the comment on that path now says which of the two it is.

**What the struct emitter still owes, read off `tests/08-structs.ag` rather than
guessed** — this is the next unit of work, and the corpus file is the checklist:

1. the `typedef struct Name { … } Name;` and its stored `var` fields;
2. §7.16's anonymous base member (`struct Point;`, first, offset 0);
3. §7.25's computed property → a **getter function**, and its distinction from a
   stored field (which has no accessor at all);
4. §7.25's method → a C function prefixed `StructName_` whose first parameter is
   `self`, with §7.14's inference deciding `Point *self` vs `const Point *self`
   **from the body** — no keyword;
5. `self.x` → `self->x`, **and a BARE field name inside a method** — 08 writes
   `return x + y` as well as `return self.count`, so a field name in a method
   body has to reach `self->` too, which needs the enclosing struct's fields (an
   inherited one needs the base's declarations, and a base in another file is
   §9.5's importer);
6. the call site: `a.length()` → `Point_length(&a)`, which needs a
   **name → struct-type environment** (params, locals and fields) that the
   emitter does not have — 08's `func total(a: Point, b: Point)` forces it, since
   a free function calls a struct method on its parameters;
7. the inherited call's **upcast**
   (`p3.length()` → `Point_length((const Point *)&p3)`);
8. **the struct initializer — DECIDED (user, 2026-09): a declared `init` takes
   `self` BY VALUE and returns the struct.** `init(start: Int32)` on `Counter`
   emits `Counter Counter_init(Counter self, int32_t start)`, and the body's
   `return self;` is the struct it hands back (the same append §7.49 already makes
   unconditional for a class initializer). The reasoning is the doc taken
   literally rather than harmonised: §7.8 says a value type's initializer is
   "an ordinary `method … -> Self`, no special form", §7.25 says a
   `method … -> Self` on a struct is a function returning the struct **by value**,
   and 08's own comment already says "the emission passes `self` by value". The
   out-parameter reading (`void Counter_init(Counter *self, …)`, the shape every
   other mutating struct method takes) was rejected because it would make
   `Counter(start: 3)` un-usable as an expression, and §6's literal reading
   constructs inline.
   - **The consequence this creates, recorded so it is not discovered: the CALL
     site has to supply `self`.** `Counter(start: 3)` is not a free-standing
     literal when a declared initializer exists — it becomes
     `Counter_init(<some zeroed Counter>, 3)`, so the emitter needs a source for
     that value (a zeroed compound literal is the obvious one). Corpus 08
     declares `init` and never calls it, so this is not exercised yet and is
     pinned the first time a call appears rather than guessed now.
   - The two initialized forms therefore differ, and deliberately: with **no**
     declared initializer, `Point(x: 1, y: 2)` is §6's literal
     (`(Point){ .x = 1, .y = 2 }`, the memberwise set); **with** one, the same
     syntax is a call to the declared function. Both are "construct", which is
     the consistency §7.48 was reaching for.

- The type table (`sterling-syntax.md` §4) and its **reference/value rule** — a
  class type is a reference, a scalar and a **struct** are values; declared
  structs (`struct`), imported ones (`NSRange` and friends), struct literals (a
  C compound literal), **struct methods** (the function lowering, the `mutating`
  / `const` receiver, the by-address call site), and **struct inheritance** —
  the anonymous member at offset 0, the upcast on an inherited call, and the
  `-fms-extensions` contract behind it (§3.15, **now confirmed wrapper-global**)
  (`sterling-syntax.md` §5–§6); `import` and the generated `@class` forward
  declarations and `#import` ordering; calling imported C functions with
  checked-and-dropped labels.
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

- **Host suite first.** What exists is `make sterlingc-check`, five legs over
  `tools/sterlingc.sh` (`tools/sterlingc-compile.sh` is the fifth). It needs no
  QEMU, and the compiler's correctness lives here because that is also where the
  bugs are.
  - `--golden` — §2's specimen byte-for-byte, **and** every
    `tools/sterlingc/tests/golden/<Class>.ag` against the `<Class>.h`/`.m`
    checked in beside it. §2 covers no local, no assignment, no send and no
    operator, so the specimens are what actually hold the emitter. A golden is an
    ASSERTION and not a recording: when one changes, the diff is read and a
    decision is made about which side is wrong.
  - `--corpus` — every `tests/*.ag` lexes and parses, with the parse and emit
    counts reported **separately**. One count standing for both is how the parse
    number fell when only the emitter changed.
  - `--reject` — every `tests/reject/*.ag` rejected **by the front end**.
  - `--refuse` — every `tests/refuse/*.ag` *accepted* by the front end and
    refused **by name** by the emitter, the message matched against the
    `.expect` beside it. A refusal for the wrong reason is not a pass.
  - `--compile` (`tools/sterlingc-compile.sh`) — the emitted code is fed to clang
    under `-fobjc-arc` against libobjc2. A diff proves the text is what was
    expected; it does not prove what was expected was C.
  - Because emission can fail, the front end has `--parse`: without it a leg that
    means "the parser rejected this" would also be satisfied by "the emitter has
    no rule for this", and the `--reject` leg would pass a case the front end
    accepted.
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
  `userland/Foundation/` contains **no** `_Nonnull`/`_Nullable`/
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
3. ~~**The `-fms-extensions` placement** (§3.15): wrapper-global, or the
   named-first-member alternative that avoids the dialect change. Decide before
   K3.~~ **ANSWERED (user, 2026-09, when K3 was opened): wrapper-global
   `-fms-extensions`.** Struct inheritance emits the anonymous tagged member. The
   named-first-member alternative is closed; the dialect cost is accepted and
   §3.15 records it, including the proof obligation it creates for K3.
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
     build module"* was never about modules: `Foundation.h` imports its siblings as `<Foundation/…>`,
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
