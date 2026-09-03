# OS profile — what these documents describe

A characterization of the operating system designed in this corpus
(docs/fsh-proposal.md, config-design.md, gui-e-toolkit.md, app-model.md,
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
- **Plain C everywhere** — kernel, GUI, apps; no Objective-C, no C++,
  no runtime, struct-embedding vtables.
- **Small, simple, educational** (the Fiwix lineage): readable ANSI C,
  single-lock scheduler, eager FPU, staged milestones with acceptance
  gates.

## The kernel

- x86-64, UEFI-booted, POSIX-compatible; boots straight to a **BFS
  root** (no initrd/RAMdisk), options from `/System/ESP/kernel.conf`.
- **Filesystems**: BFS (OpenBFS) = the only writable filesystem
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
- **GUI**: OS-native compositor + full widget toolkit (3D bevels,
  `view` base, `canvas`, `springs_and_struts`); no Wayland, no X11; a
  dock, wallpaper, Miller-column file manager; app bundles
  (`HelloWorld.app/` dirs with `.conf` manifests).
- **Initial release**: eight apps — Workspace, Terminal, Editor,
  Settings, Viewer, Calculator, Installer, Disks — plus `config`,
  `acl`, and `mkfs` tools.

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
