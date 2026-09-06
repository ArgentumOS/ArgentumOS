# kernel.conf — ESP boot config implementation plan

Status: **PLAN (2026-09) — design decided (docs/config-design.md §12,
docs/fsh-proposal.md §9.2 Q8), nothing implemented.**

## 0. Goal

Implement the decided kernel.conf boot configuration: the kernel reads
its command-line options from a **libconfig-format file next to the
kernel on the ESP**, replacing today's hardcoded cmdline string. The
file uses the same `.conf` grammar as all FNX config; a real
firmware/cmdline-provided override wins over individual keys when
present.

## 1. Current state (grounding)

- **The kernel "cmdline" is a baked constant**: `kernel64/kreal64.c`
  builds `static char cmdline[] = "fnx console=/System/Devices/Serial/
  Port0 root=/System/Devices/Disk/AHCI/Disk0/WholeDisk"` and hands it
  through the multiboot-style `mbi` struct; `kernel/main.c` +
  `include/fnx/kparms.h` parse it. No external input exists.
- **No ESP file I/O**: `kernel64/efi_stub.c` (230 lines) never touches
  `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL`/`LoadedImageProtocol`; its only
  boot-service use is `ExitBootServices` (line 218).
- **No FAT driver** in the kernel (userland FAT32/ExFAT is a separate
  planned item), so early ESP access must use **UEFI boot-services file
  I/O before `ExitBootServices`** — exactly as config-design §12
  anticipates ("the FAT32 driver, or UEFI boot-services file I/O before
  ExitBootServices until then").
- Decided design to implement: §12 (location next to the kernel,
  `.conf` grammar, cmdline overrides keys, small kernel-side
  flat/dot-nested parser accepting canonical output, `system.kernel`
  pinned single-file domain aliased to `/System/ESP/kernel.conf` when
  the ESP is mounted).

## 2. Design

### Location and read path

- **File: `kernel.conf`, next to the kernel image on the ESP** — i.e.
  in the same directory as the loaded `BOOTX64.EFI`, derived at runtime
  from the Loaded Image Protocol's FilePath (strip the image filename,
  keep the directory). Rationale: multiple kernels/boot entries each
  keep their own config; this is the user's stated intent ("next to the
  kernel"). Note: §12's early-read rule says a plain `\kernel.conf` on
  the ESP — same-directory satisfies the spirit and is the exact
  reading of "next to the kernel"; update §12's wording when
  implemented if the root-vs-same-dir nuance matters.
- **Reader runs in the EFI stub before ExitBootServices** (M0): derive
  the boot volume from the Loaded Image Protocol device handle, open
  the volume via `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL`, open `kernel.conf`
  in the image directory, read it into a boot-services pool buffer,
  and hand (pointer, size) to the kernel through the existing
  boot-info handoff alongside the cmdline.
- Missing file → defaults, silently (current behavior unchanged).

### Parsing and precedence

- **Kernel-side `.conf` subset parser** (M1): small read-only parser in
  `kernel/` — flat + dot-nested keys (`console`, `system.kernel.root`),
  string/int/bool values (ints incl. `0x`), bare-or-quoted strings,
  `#` comments, empty `key =`, duplicate-key last-wins — accepting
  canonical `config`-tool output per §12. Sharing: the grammar is one;
  a **shared conformance test corpus** (same inputs exercised by both
  the userland parser and the kernel parser) keeps them honest where
  true source-sharing across kernel/userland isn't practical.
- **Precedence**: compiled-in defaults (today's baked string becomes
  the default layer) < `kernel.conf` keys < a real firmware cmdline's
  individual keys when present (§12 rule).
- **Option mapping**: v1 maps the existing kparms keys
  (`console=`, `root=`, `rootfstype=` — see `include/fnx/kparms.h`) and
  `verbose`; later corpus keys (`services=`, `driver.<name>.enabled`)
  extend the same mapping table.
- **Errors**: parse error on a key → `printk` warning + fall back to
  default for that key (never halt on a bad boot option).

## 3. Milestones

- **M0 — EFI-stub ESP read**: LoadedImageProtocol → boot volume →
  open volume → open `kernel.conf` next to the image → read into a
  boot-services buffer; extend the handoff to carry (ptr, size).
  Acceptance: boot logs show the file's byte count when present, and
  clean defaults when absent; verified in QEMU with a crafted ESP.
- **M1 — kernel `.conf` subset parser + kparms wiring**: parser in
  kernel/, mapping table for v1 keys, parse before `mount_root`,
  precedence per §2. Acceptance: `kernel.conf` with
  `root = ...` / `console = ...` changes the boot (serial output +
  mounted device prove it); bad value → warning + default; conformance
  corpus passes against both parsers.
- **M2 — tooling**: `tools/mkesp.sh` places a commented `kernel.conf`
  template next to `BOOTX64.EFI`; guest harness boots with an
  overridden `root`/`console`/`verbose` from the file. Acceptance: one
  edited key changes the boot; the template round-trips through the
  parser (canonical form accepted).
- **M3 — `system.kernel` domain (dependent)**: the `config` CLI reads/
  writes the ESP file via the pinned `system.kernel` alias
  (`/System/ESP/kernel.conf`) — requires the ESP mounted at
  `/System/ESP` (FSH Q2 mount, separate work). Until then the kernel
  reads the file; userland editing of it is not wired.

## 4. Open items

- Root-vs-same-directory nuance vs §12 wording (lean: same directory as
  the image; update §12 at implementation).
- v1 key set confirmation (`console`/`root`/`rootfstype`/`verbose`).
- Unknown-key policy (lean: warn + ignore in v1).
- Buffer sizing/cap for kernel.conf and pool-vs-page allocation in the
  stub.
- Sequencing: this is boot/ESP work — the user slots it where they
  wish relative to the filesystem → dynamic-linking → Momo roadmap.

## 5. Non-goals

- No kernel FAT driver (the boot-services read covers v1; a kernel FAT
  reader is the later userland-FAT item's concern).
- No kernel writes to the ESP in v1 (editing flows through `config`
  once the ESP mount exists, M3).
- No scopes/arrays in the kernel parser (read-only flat/dot subset per
  §12).
