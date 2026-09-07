# kernel.conf — ESP boot config implementation plan

Status: **DONE (2026-09) — M0..M3 complete.** M0 (EFI stub reader +
fnx_kconf handoff), M1 (kconf parser + kernel_conf_apply), M2 (ESP
template staged by mkesp.sh), M3 (config `system.kernel` domain). The
FAT driver now mounts the ESP volume at /System/ESP (fatfs-driver-plan
M0..M2c); M3's pinned alias points at the bootloader's own file
(/System/ESP/EFI/BOOT/kernel.conf), so `config write system.kernel`
edits the REAL ESP file and the next boot's firmware applies it —
verified end-to-end (write `recovery = true`, reboot -> "kernel.conf:
applied 'recovery'" -> the recovery shell).

## 0. Goal

Implement the decided kernel.conf boot configuration: the kernel reads
its command-line options from a **libconfig-format file next to the
kernel on the ESP**, replacing today's hardcoded cmdline string. The
file uses the same `.conf` grammar as all FNX config; a real
firmware/cmdline-provided override wins over individual keys when
present.

## 1. Current state (grounding)

- **The kernel "cmdline" is a baked constant**: `kernel/boot64/kreal64.c`
  builds `static char cmdline[] = "fnx console=/System/Devices/Serial/
  Port0 root=/System/Devices/Disk/AHCI/Disk0/WholeDisk"` and hands it
  through the multiboot-style `mbi` struct; `kernel/main.c` +
  `include/fnx/kparms.h` parse it. No external input exists.
- **No ESP file I/O**: `kernel/boot64/efi_stub.c` (230 lines) never touches
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
  pinned single-file domain aliased to `/System/ESP/EFI/BOOT/kernel.conf` when
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
  **DONE** — see §3.1 for the implementation notes.
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
  parser (canonical form accepted). **DONE** — see §3.3.
- **M3 — `system.kernel` domain (dependent)**: the `config` CLI reads/
  writes the ESP file via the pinned `system.kernel` alias
  (`/System/ESP/EFI/BOOT/kernel.conf`) — requires the ESP mounted at
  `/System/ESP` (FSH Q2 mount, separate work). Until then the kernel
  reads the file; userland editing of it is not wired. **Config side
  DONE (2026-09)** — see §3.4; the mount is the remaining dependency.

### 3.1 M0 implementation notes (2026-09)

- Reader: `esp_read_kernel_conf()` in `kernel/boot64/efi_stub.c`, called
  from `efi_main` **before the memory map is taken** — file I/O allocates,
  and any allocation between the map call and `ExitBootServices` would
  invalidate the map key.
- Handoff: `struct fnx_kconf { char data[8192]; unsigned int size; }`
  (`.bss`, zeroed by the PE loader; `size = 0` means absent → defaults).
  The 8KB static buffer answers the §4 pool-vs-page/sizing open item (a
  v1 template is ~1-2KB); living in the image means no free-memory hazard
  for the M1 parser.
- The image's directory comes from `LoadedImageProtocol->FilePath`
  (device-path walk, last component stripped), so multiple kernels each
  keep their own `kernel.conf`.
- Gotchas fixed during bring-up: `EFI_OPEN_PROTOCOL_GET_PROTOCOL` is
  `0x02` (not `0x04`, which is TEST_PROTOCOL and does not return the
  interface); `L"..."` literals are `wchar_t` (4-byte) under clang, so
  CHAR16 paths are built by widening a plain char array.
- Verified in QEMU: `kernel.conf` present → `[kernel.conf] read 127
  bytes` then a normal boot; absent → `[kernel.conf] kernel.conf not
  found (compiled-in defaults)` then a normal boot.

### 3.2 M1 implementation notes (2026-09)

- `kernel/kconf.c` (+ `kernel/kconf.h`): the read-only .conf subset
  parser — line-based `key = value`, `#` comments, dot-nested keys kept
  flat, bare/quoted strings, `true`/`false`, ints incl. `0x`, empty
  `key =`, malformed lines skipped with a warning (never a halt). No
  kernel dependencies: the same object builds on the host.
- `kernel_conf_apply()` in `kernel/multiboot.c`: iterates `fnx_kconf`,
  maps each key onto `kparamval_table` (value entries via `check_param`,
  flags on a true value) and prints what it applied. Called from
  `start_kernel()` after `multiboot()` + `set_default_values()` and
  before `mount_root()`, so precedence is compiled-in cmdline (default
  layer) < kernel.conf keys < a future real firmware cmdline. The parser
  is generic over the whole param table: any existing kernel option
  (root, console, rootfstype, recovery, ro, ramdisksize, ...) works from
  the ESP file; unknown keys warn + are ignored.
- Conformance corpus (`tools/kconf_corpus.sh` + `tools/kconf_corpus/`):
  five files (basic, quoted, bool/int, empty, dot-keys) parsed by both
  the kernel parser and the userland libconfig parser must produce the
  same effective key/value listing — all-pass.
- Guest-verified in QEMU:
  - `recovery = true` → `kernel.conf: applied 'recovery'` → recovery
    shell boots (an ESP file changed the boot; overrides the default
    layer);
  - invalid `root = /no/such/device` → `invalid value ... using default`
    warning + normal boot with the compiled-in default root;
  - valid `console`/`root` overrides → applied lines + normal boot.
- v1 key note: `verbose` is not yet a kernel option (no consumer); it
  currently lands in the unknown-key warn+ignore path. Add it when
  something reads it.
- Bug found by the corpus bring-up: `kconf_next()` looped forever at
  EOF on a file not ending in a newline (EOF-on-blank fell through into
  the parse section instead of returning 0). Fixed + covered by the
  corpus driver's exit code.

### 3.3 M2 implementation notes (2026-09)

- `tools/esp-kernel.conf`: the commented template (canonical `.conf` form,
  FSH header comments). Everything is commented out, so a stock
  `make mkesp`/`run-uefi` ESP boots exactly as before; uncommenting a line
  overrides that option for this kernel. mkesp.sh stages it next to
  `BOOTX64.EFI` (`EFI/BOOT/kernel.conf`).
- Template round-trip verified through BOTH parsers: the kernel parser and
  the userland libconfig parser both reduce the unmodified template to
  zero keys with no errors.
- Guest-verified: stock ESP (template present, commented) → normal boot
  (`[kernel.conf] read 1481 bytes`, no applied/warning lines, INIT alive);
  uncommenting `recovery = true` in the ESP copy → `kernel.conf: applied
  'recovery'` → recovery shell boots. One edited key changes the boot.
- Note: every mkesp'd ESP now carries a (commented) `kernel.conf`, so the
  EFI stub no longer logs "not found" on stock images — the file is
  always present; absent-file behavior remains the fallback for ESPs that
  predate M2.

### 3.4 M3 implementation notes (2026-09, config side)

- `userland/libconfig.c`: the pinned `system.kernel` alias — `domain_path()`
  short-circuits to `<config_root>/System/ESP/EFI/BOOT/kernel.conf` for that one
  domain, ignoring the scope entirely. Because every scope resolves to the
  same path, the scope merge degrades to a single file read (system scope
  wins) and no scope-root `system.kernel.conf` is ever consulted. Honors
  `FNX_CONFIG_ROOT` like every path, so host tests can re-root the alias
  (in the guest `config_root() == "/"` gives the exact §12 path).
- `config_is_pinned(domain)` exported in `userland/libconfig.h`; the
  `config` CLI skips its user/shared shadow warnings for pinned domains
  (there is no second scope to shadow the ESP file).
- The canonical writer needs no ESP-specific handling: `config write` uses
  the existing atomic writer (temp + fsync + rename) into the ESP
  directory, exactly as §12 anticipates.
- Unavailable semantics verified: with no file at `/System/ESP/EFI/BOOT/kernel.conf`
  the domain resolves to nothing (`read` reports not found), matching the
  designed "unmounted ESP → domain unavailable".
- Guest-verified through the `config` CLI on the pinned domain:
  `write system.kernel recovery true -type bool` + `read` → `true`;
  `write system.kernel root /System/Devices/...WholeDisk` + `read` → the
  path; `cat /System/ESP/EFI/BOOT/kernel.conf` shows the canonical file
  (`recovery = true` / `root = ...`); `rm` removes it. The canonical file
  the writer produces also parses cleanly under the kernel `.conf` parser
  (round-trip). No scope-root file is consulted.
- Caveat (the remaining M3 dependency): the in-guest test wrote the XBFS
  root's `/System/ESP/EFI/BOOT/kernel.conf` copy. Until the ESP is actually mounted
  there (FSH Q2 — the FAT32 driver work), the file the `config` CLI edits
  is NOT the ESP the firmware booted from; boot consumption still comes
  from the real ESP via the M0 stub read.

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
