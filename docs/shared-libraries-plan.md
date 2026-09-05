# Shared libraries on FNX

Status: **DECIDED (design, 2026-09) — nothing implemented.** All design
decisions below were settled in conversation; kernel/toolchain work
described is the implementation backlog, not started.

## 1. Why

Static executables are fine now but a long-term burden: every binary in
the rootfs duplicates its libc image (size + RAM). FNX will move to
shared libraries. This document records the decided design.

## 2. Decisions

### 2.1 Scope: dynamic linking first; dlopen deferred, probably never

- Milestone = shared-libc dynamic executables (loader/linker/relocation
  story only).
- **dlopen is explicitly deferred and probably never implemented.** No
  current or planned FNX code needs runtime plugins: the Xfb server was
  chosen partly to avoid modular Xorg's dlopen; the CDE-fork plan's apps
  link a toolkit, they don't dlopen one. musl supports dlopen only in
  dynamic mode, so the deferral doesn't block the first milestone — it
  just means no plugin surface is promised.

### 2.2 Library placement (FSH)

Three tiers, decided:

- **First-party** (ships in the default OS install) → `/System/Libraries`
  (libc.so, ld.so, the X stack, the toolkit, everything we ship).
- **Third-party** shared libraries (user-installed, outside our support)
  → `/Shared/Libraries`.
- **App bundles may self-contain** their own libs whenever they need.

`/System` stays the OS-owned tree; `/Shared` stays the cross-user shared
space. musl's ld.so search path is baked at build time — it becomes
another `third_party/musl-fsh.patch` constant
(`/System/Libraries:/Shared/Libraries`), joining the existing path
patches.

### 2.3 PIE/ASLR

**Fixed-base dynamic first.** Executables dynamic but non-PIE; the kernel
loads the interpreter at `AT_BASE` with no randomization. ASLR is
additive later (the loader logic is identical; randomization is just a
chosen base) — deliberately deferred so the first dynamic boots are
deterministic and debuggable.

### 2.4 End-state: everything dynamic

All binaries (including `/System/Tools` and PID 1 — the kernel mounts the
root before exec'ing init, so ld.so on the root is available; no
bootstrap deadlock) and all libraries become dynamic.

**One static recovery shell is kept** — insurance when
`/System/Libraries` is corrupt or missing (doctrine-consistent: it is our
binary, static by choice).

### 2.5 Versioning

- Versioned sonames as **bookkeeping** (the loader needs them for
  `NEEDED` matching), not ABI promises.
- **No ABI-compat layer**: we ship every library and every binary and
  rebuild the world together per release; no third-party binary
  ecosystem depends on soname stability. Third-party software opts into
  `/Shared/Libraries` at its own risk (the "unhelped" doctrine).
- Upgrades are whole-world flips, not mixed-version states.

## 3. The update / consistency model (decided)

Requirements that shaped it: only one root (no parallel bootable worlds);
no "jiggery-pokery" (no staging dirs, symlink farms, active-version
pointers, version trees inside `/System`); `/System/Configuration` is
writable live state and must persist.

Decided model:

- **One root; the live tree is a plain tree and is never surgically
  mutated while running.**
- **The system updater is a static application loaded from the
  downloaded update location at reboot** and run *before* the normal
  userspace boots. It is static because it must run before
  `/System/Libraries` is trustworthy — and precisely because it needs
  nothing from the target's libraries, it is safe to replace them. The
  static-executor pattern is the same one as the static recovery shell:
  repair shell and updater are siblings (static code that runs when the
  dynamic world can't be trusted).
- The updater applies the update to the **quiescent target tree** (the
  target isn't running during the update — no hot-swapping under live
  processes). Consistency is required at next boot, not at every instant
  of the write.
- **Configuration and Users persist by construction**: they are data, not
  part of the update set — the updater writes only code (Libraries,
  Tools, Applications).
- **Boot-time safety net**: before the desktop comes up, a cheap check
  that every dynamic binary's `NEEDED` resolves against the live tree.
  Failure → static recovery shell with a clear "world mismatch" message
  instead of a cascade of exec failures.
- The static recovery shell doubles as the repair-and-hand-off
  environment if a target is broken mid-update.

**Update flow (sketch):** running system downloads an update to a
boot-readable location → sets a pending-update signal → reboot → the boot
path sees the signal and loads+runs the static updater from the update
location → updater writes the new code world → normal boot proceeds →
boot-time `NEEDED` check passes → desktop.

## 4. What the tree looks like today (grounding)

- `fs/elf.c` (386 lines) already builds the full 64-bit exec stack with
  auxv (`AT_PHDR/PHENT/PHNUM/PAGESZ/BASE/FLAGS/ENTRY/UID/EUID/GID/EGID/
  RANDOM`, 16-byte aligned) — the dynamic-linker contract is half
  written. Missing: `ET_DYN`/`PT_INTERP` handling (it maps a static
  image and jumps to `e_entry`; `AT_BASE` is currently always 0).
- musl is configured with defaults (`--target=x86_64` in the `musl64`
  rule), so `libc.so` and `ld-musl-x86_64.so.1` are already produced —
  unused. `tools/musl-gcc64.sh` forces `-static` ("FNX has no dynamic
  linker").
- The FSH Q1 porting linter (`tools/fshlint.py`) **zero-allows PT_INTERP**
  under `/System/Tools` today — a transitional rule that flips once
  dynamic binaries exist (see §6).
- The X stack in `.build/x11-prefix` (libX11, libxcb, pixman, …) is
  built static by its meson recipes; going shared means PIC rebuilds
  (a rebuild, not a port).
- FSH root is five top-level dirs; `/System` owns the OS tree;
  `/System/Configuration` holds writable `system.config.*.conf` state.

## 5. Kernel work (the contained core)

Fixed-base dynamic exec in `fs/elf.c`:

- Accept `ET_DYN`/`PT_INTERP`: when a `PT_INTERP` segment is present,
  load the interpreter image (from `/System/Libraries/ld-musl-x86_64.so.1`
  as resolved by the baked search path) at `AT_BASE`; map the main
  image's `PT_LOAD` segments; describe the **main image's** phdrs in
  auxv (`AT_PHDR` etc.); enter at the interpreter's entry point and let
  musl's ld.so self-relocate and load dependencies.
- `AT_BASE` becomes the interpreter load base (0 for static images).
- Boot-time pending-update handling (see §3): the boot path checks the
  signal and loads the static updater before init. Kernel-side is the
  natural home (FNX's boot path is UEFI stub → kernel, and the kernel
  does the real boot work). The update location and the bootloader-vs-
  kernel check are open items (§7).

## 6. Toolchain / build backlog

- Build musl shared and install `libc.so` + `ld-musl-x86_64.so.1` into
  `/System/Libraries`; add the FSH search-path patch.
- Flip `tools/musl-gcc64.sh` off `-static` (default dynamic); provide a
  static override for the recovery shell and the updater.
- Convert bottom-up by dependency: libc first; then the X stack +
  libconfig (PIC rebuilds); toolkit later with its own plan. Apps flip
  to dynamic once everything they link is shared (mixed static/dynamic
  states are fine as long as deps resolve).
- fshlint: the `PT_INTERP` zero-allow flips to "dynamic is the norm";
  the static set (recovery shell, updater) is the explicit exception
  list.

## 7. Open items

- Where the downloaded update location lives (a dedicated boot-readable
  area/partition) and the pending-update signal's form.
- Bootloader vs kernel responsibility for the pending-update check
  (lean: kernel).
- Final shape of the static set (recovery shell + updater; anything
  else?).
- libc/ld soname naming under FSH conventions.
- ASLR: a later, separate milestone (additive to the fixed-base loader).

## 8. Non-goals

- No dlopen/plugin surface (deferred, probably never — §2.1).
- No in-place live updates of the running tree.
- No multiple bootable roots / rollback-by-old-root worlds (one root).
- No ABI-compat or third-party binary support promises.
