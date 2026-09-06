# GNUstep evaluation

Status: **REJECTED (2026-09)** — evaluated as candidate B and closed.
Rationale: **"I don't want to own a GNUstep fork."** musl can host
GNUstep (Alpine is existence proof), but the port would be ours to
sustain alone on a libc that refuses glibc-compat shims; FreeBSD's libc
(which would fit GNUstep) is kernel-entangled and can't sit on the
from-scratch FNX kernel; glibc likewise. The kernel is musl-shaped and
immovable. GNUstep therefore required owning a large foreign
fork+runtime stack that the OS would carry forever — rejected. The
plists/config-format and FSH/bundle decisions recorded below were
conditional on GNUstep adoption and are now **moot** (kept for the
record). The toolkit direction returns to candidate A (Momo/Motif fork)
vs a from-scratch native toolkit from the momo_* spec.

Toolchain doctrine is unchanged by this: one system compiler = Clang,
languages open per subsystem (the plain-C rule stays gone).

## 1. Why it's a candidate

GNUstep is the living embodiment of the paradigm the Momo spec was
re-inventing in C: the global menubar (`NSApp` main menu), springs and
struts (`autoresizingMask`), app bundles, the workspace model. If the
desired lineage is NeXT, GNUstep is the genuine article — not a Motif
fork with NeXT ideas bolted on.

## 2. Enabling decisions (already made)

- **Toolchain doctrine (os-profile, 4229be1)**: one system compiler =
  Clang; languages open per subsystem. Plain-C-only is gone, so
  Objective-C is legitimate. clang-19 is host-available; LLVM libc++
  19.1.7 already pinned at .build/llvm-cxx.
- **Dynamic world (docs/design/shared-libraries-plan.md)**: the toolkit lands
  after the dynamic-linking switch — shared `libobjc2.so`,
  `libgnustep-base.so`, `libgnustep-gui.so` in /System/Libraries. ObjC
  runtimes fit shared far better than static (class/category
  registration), so dynamic linking favors GNUstep.

## 3. The FSH question — possible (decided direction)

GNUstep is root-mapped, not path-hardwired (GNUSTEP_SYSTEM_ROOT /
_LOCAL_ROOT / _USER_ROOT). Mapping:

| GNUstep wants | FSH home |
|---|---|
| shared libs (base/gui/objc runtime) | /System/Libraries |
| Headers/, Makefiles/ (dev data) | /System/Libraries/GNUstep/{Headers,Makefiles} (one sanctioned container) |
| user home / per-user state | /Users/<user> |
| temp | /System/Temporary Files |
| user apps | /Users/<user>/Applications |
| launch/discovery | launcher sets GNUstep env per mapping (single .conf/plist source) |

**Appdir bundles: adopted** — GNUstep `.app/` bundles are exactly the
FSH app-bundle shape; first-party apps in /Applications, user apps under
/Users/<user>/Applications, discovered by directory scan.

## 4. Config format — plists, full replacement (decided)

If GNUstep is adopted, **plists replace libconfig as the system config
format** (pure shape): plists everywhere, including a **small C
plist-subset parser** used by the kernel (kernel.conf) and by musl
(identity domains: passwd/group/hosts/network/shells/mounts currently
read in-libc via the musl-fsh renderer). Foundation's NSUserDefaults
domain semantics map onto FSH user→shared→system.

Consequences to record when this triggers:
- The `.conf`/libconfig corpus (docs/design/config-design.md,
  userland/libconfig.c, the `config` CLI, system.config.*.conf domains)
  becomes the predecessor design.
- Momo-spec statements ("X resources leave in favour of libconfig",
  `.conf` theming) become "...in favour of plists" where GNUstep is the
  toolkit.
- Open format choice: old-style ASCII plists (readable, editable,
  cousin of .conf's grammar, trivially parseable by the small C parser)
  recommended over XML/binary.

## 5. Open items / next steps

- **Gate 1 — libobjc2 feasibility probe (NOT run; user said wait)**: can
  clang-19 build libobjc2 as a *shared* lib against musl, with a
  dynamic ObjC hello running through ld-musl on the host? Deferred
  until the user lifts the hold.
- Plist flavor decision (ASCII recommended).
- GNUstep-make/base/gui/back fork surface and the X11 `back` backend
  evaluation (maturity, fonts, the global-menu reality on X11).
- The "no external standards" identity question: adopting plists as the
  canonical format is a deliberate reversal of that pillar, tied to the
  toolkit decision.

## 6. Relationship to candidate A

The momo_* design spec (typed API, two-container layout, Xft, global
WM-owned menubar, catalog, Settings reference) stays valid as a
specification regardless of the implementation path. If GNUstep wins,
much of it is re-expressed in AppKit terms (much is already native
there); if Momo wins, plists-full-replacement does not apply (libconfig
stays).
