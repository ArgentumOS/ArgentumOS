# Before the release — engineering items

Status: **working list.** Things that must land before the first release and are NOT application scope —
that is `docs/design/initial-release.md`. Each item states what is TRUE TODAY, so it can be checked rather
than remembered.

## 1. Prefix our internal Objective-C declarations `AG`, not `FN` (user, 2026-09-28)

**WHAT: every internal declaration in our Objective-C sources takes the house prefix `AG`** — the prefix
`AGFS` already uses. `FN` is the FNX-era prefix, retired for these.

**MEASURED 2026-09-28, so this is not re-estimated later:**

| tree | `FN…` | `Fn…` | `fn_…` |
|------|------:|------:|-------:|
| `userland/Foundation` (the library) | 1,026 | 56 | 3,026 |
| `userland/tests` (the Objective-C probes) | 535 | 263 | 1,979 |
| `userland/AppKit` | 0 | 0 | 183 |
| `userland/CoreGraphics` | 0 | 0 | 12 |

So it is classes and types (`FNTextBreaking`, `FnProbe`), enum-ish constants (`FN_DF_MAX`, `FN_PUT`), and C
statics (`fn_hex_value`, `fn_line_check_range`).

**TWO EXCLUSIONS, stated so a sweep does not take them:**

* **`FNX` as a NAME stays.** It is the kernel's engineering name by the recorded naming split (Argentum =
  the house brand, FNX = the kernel engineering name), and it appears in prose and inside strings such as
  `getenv("FNX_CONFIG_ROOT")`. Only `FN…` **identifiers that are ours** change.
* **The kernel is out of scope.** It has no `FN*` declarations at all — its 64 matching files are `FNX…`
  macros and prose (`FNX_WINDOW_MIN`, `FNX_KCONFIG_MAX`) — and this item is about the Objective-C
  libraries.

**HOW:** a scripted, assert-once rename (`\bFN[A-Z]\w*` → `AG…`, `\bFn[A-Z]\w*` → `AG…`,
`\bfn_[a-z0-9_]*` → `ag_…`) over the Objective-C trees, then a build and the test tier; **the docs that name
a renamed identifier are corrected in the same commit**, because a plan naming a symbol that no longer
exists is drift. Do it as ONE commit, when nothing else is in flight — it touches 300+ files.

## 2. The parameterization set: two gaps the compiler found

The clause that measures this is `tools/foundation-sweep.py --parameterized` / `--check` (139 findings
across 13 classes on 2026-09-28; **103 as of 2026-09-29, because M1's family reached 0**).

* **`NSOrderedCollectionDifference` IS parameterized by Apple and is NOT in the clause's class list.** Our
  compiler refused `NSOrderedCollectionDifference<ObjectType>` in `NSArray.h` while Apple's own `NSArray.h`
  writes exactly that. The list must grow a row, and our class must gain the parameter.
* **`NSEnumerator`'s class line is parameterized as of 2026-09-28** (`<ObjectType>`); **its methods still owe
  theirs**, so the clause still reports it.
* **AND M1 FOUND A THIRD SHAPE OF THE SAME GAP, worth knowing before M2:** Apple parameterizes the MUTABLE
  class's own plist conveniences as a SEPARATE category in its `NSArray.h` (its lines 187-190), answering
  `NSMutableArray<ObjectType> *`. So "which category declares what" is part of matching Apple, not just
  "which declarations name a parameter".

## 3. Foundation stacks: the class-cluster milestones (§C.5 of `foundation-clusters-plan.md`)

**M1 (`NSArray` / `NSMutableArray`) IS COMPLETE 2026-09-29** — the runtime half is `3baf993f`, and the
compile half plus the probe's family section landed with this note. Three corrections to what this file
said before, all measured:

* **"The mutable concrete class cannot inherit the immutable concrete's storage" was WRONG.** The storage is
  the FRONT's own ivars, so every concrete class — the mutable one included — inherits the same layout. The
  real obstacle was different: **`NSMutableArray` defines NO initializers at all** (31 methods, no `-init`,
  no `-initWithObjects:count:`, no `-initWithArray:`, no `-initWithObject:`; it inherits all four from the
  front), so the class-choosing guard had to be a MEMBERSHIP test — a kind test would have sent every
  mutable construction into the immutable family.
* **The read-side rewrite was 71 sites over 26 methods, not 105 sites** — the larger number counted both
  halves of the file.
* **AND ONE THING ONLY THE PROBE COULD HAVE FOUND: the empty case must be chosen BEFORE the storage check.**
  `+array` calls `-initWithObjects:NULL count:0`, and the first guard required `objects != NULL`, so the
  empty case fell through to the general class — `[NSArray array]` answered the general class instead of the
  shared empty instance, and "the four cases are four classes" failed with it.

**ALL THREE OF M1'S ACCEPTANCE BULLETS ARE GREEN.** The compile probe landed with the case:
`tests/cases/foundation_clusters.py` compiles two snippets with `tools/musl-clang-objc64.sh` — a covariant
assignment must COMPILE, an unrelated specialization must be REFUSED — plus a NEGATIVE CONTROL that strips
`__covariant` from a MIRROR of the include tree and requires the same snippet to fail for THAT reason, so
the first check cannot be passing on the compiler's goodwill. Two instrument facts were MEASURED, and each
would have produced a vacuous check: **without `-Werror=incompatible-pointer-types` the refusal snippet
compiles (exit 0)** — the flag IS the instrument; and the mirror must be SYMLINKS to the rest of `userland/`
with only `Foundation/` copied, because a copy of `Foundation/` alone cannot compile at all
(`CoreGraphics/CGGeometry.h`), and a failure for an unrelated reason reads exactly like the instrument
working. **M2 (`NSDictionary` / `NSMutableDictionary`) is next**; §2's two gaps remain.
