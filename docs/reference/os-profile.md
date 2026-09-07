# OS profile — what these documents describe

A characterization of the operating system designed in this corpus
(docs/design/fsh-proposal.md, config-design.md, gui-e-toolkit.md, app-model.md,
initial-release.md, permissions-acl.md, utf8-only.md, smp-eval.md,
fpu-eval.md, gui-feasibility.md, toybox-fsh-plan.md, and the GUI
candidate survey).

---

## Identity (one paragraph)

FNX is a **from-first-principles, desktop-first personal operating
system**: a small, POSIX-compatible kernel carrying a radically original
userland that owns every layer — its own filesystem hierarchy, its own
config format, its own GUI stack, its own app packaging — and rejects
external standards on principle, yet keeps enough POSIX surface to run
mindfully-ported software.

## Philosophy & design principles

- **Originality-first, clean break.** No X11, no FHS, no plists, no
  `/bin` `/usr` `/etc` — and no compatibility symlinks either. The
  design is presented as its own, and may name its inspirations in
  passing where useful. Software is *mindfully ported*, enforced by a
  **linter gate** that fails the build on any legacy path string in a
  staged ELF.
- **One format, one model, one namespace.** The `.conf` grammar is the
  single format (user config domains, app-bundle manifests, and
  `kernel.conf` on the ESP); reverse-DNS identifiers unify app
  identity, bundles, and config domains; POSIX ACLs are the single
  canonical permissions model (mode bits are a projection); UTF-8 is
  the only encoding; one locale.
- **One system compiler toolchain: Clang** — the same compiler builds
  the kernel, the userland, and every first-party language; no foreign
  compilers in the system build. Languages are open per subsystem
  (C, C++, Objective-C as the task demands) — no runtime beyond what
  the language needs; the OS profile no longer mandates plain C.
- **Small, simple, educational** (the Fiwix lineage): readable code,
  single-lock scheduler, eager FPU, staged milestones with acceptance
  gates.

## The kernel

- x86-64, UEFI-booted, POSIX-compatible; boots straight to a **XBFS
  root** (no initrd/RAMdisk), options from `/System/ESP/kernel.conf`.
- **Filesystems**: XBFS = the only writable filesystem
  (ext2/minix/initrd removed); FAT32 + ExFAT planned (ESP + removable
  media, UTF-16↔UTF-8 LFN); ISO9660 read-only.
- **Planned**: SMP ≤ 8 vCPUs (LAPIC timers, IO-APIC, ACPI MADT, one
  locked runqueue) and SSE/FPU (eager FXSAVE, per-frame signal FP).

## The userland & desktop

- **FSH**: five root entries — `Applications`, `Shared`, `System`,
  `Users`, `Volumes` — self-similar user homes, `/System/User
  Template/` copied to each new user, a classic-workstation global
  menubar (system menu, bold app menu, File/Edit/View/Window/Help,
  right-side extras, click-time menu validation).
- **GUI**: X11 desktop — **Xfb** (native X server, owns /dev/fb0)
  with the **Shrike** from-scratch C++ toolkit (Cocoa-resemblant API,
  pixman vector chrome, real-point units) and the **Kestrel** window
  manager (global menubar); no Wayland; urxvt terminal fork.
  Design: docs/design/shrike-plan.md + docs/design/shrike-catalog.md.
- **Initial release**: eight apps — Workspace, Terminal, Editor,
  Settings, Viewer, Calculator, Installer, Disks — plus `config`,
  `acl`, and `mkfs` tools.

## Memory management (decided note)

- **Deterministic by default**: userland programs own their memory with
  explicit `free` (C) or RAII (C++/Shrike) — the collector-free world.
- **Garbage collection is opt-in, not default (decided 2026-09)**:
  the Boehm-Demers-Weiser collector (bdwgc, MIT-style; use ≥ 8.2.12,
  the musl-fixed release) is available as an opt-in library, linked
  per-program via the clang wrapper (with `GC_USE_LD_WRAP`/dlopen
  wrapping + `NO_GETENV` discipline when used). Target consumers:
  allocation-tangled programs — a future scripting/interpreter runtime,
  language-runtime-style code — not ordinary tools.
- **Default-for-every-program was explicitly rejected**: invisible-root
  hazards (pointers kept only in libc-internal or TLS storage get
  collected), no destructor determinism (conflicts with the Shrike
  RAII carve-out), stop-the-world pauses, per-binary static cost, and
  per-program audit burden — bdwgc's own maintainer frames redirect
  mode as supported-but-fragile, not turnkey.

## Tensions & risks

1. POSIX API compatibility vs. radical path redesign — resolved by the
   linter gate, but porting is real work; the ecosystem is FNX-native
   by construction.
2. Simplicity ethos vs. ambition — a ~12–25k-line widget toolkit, SMP,
   FAT drivers, an installer, and a GPT/MBR disk utility are a lot for
   a hobby kernel with a single-lock scheduler.
3. Originality vs. recognizability — desktop conventions drawn from the
   classic workstation lineage (OPENSTEP-style views, Motif's widget
   catalog, a Mac OS 9 Platinum widget look, the classic-Mac global
   menubar, NeXTSTEP's column browser and dock, macOS-style app
   bundles); the docs present the design as FNX's own and name those
   inspirations in passing.
4. UTF-8-only boldness — simplification now, at the cost of deferred
   RTL/IME/full glyphs.
5. Recorded gaps: no FAT driver yet (blocks the ESP mount + Installer
   boot write), libconfig unimplemented (P0), console-raw-mode input
   as the v1 seat.

## Bottom line

A "phoenix" in more than name: a deliberately small, radically original
desktop OS — POSIX-compatible at the API, own-everything at every other
layer, retro-modern in feel, single-format and single-model in design,
and built with a discipline (linter gates, decided questions,
feasibility evals) most hobby OSes never approach.
