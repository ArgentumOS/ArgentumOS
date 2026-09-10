# Proposal: a radical redesign of the FNX filesystem hierarchy

Status: DRAFT — for discussion. Source notes: `docs/reference/fsh-notes.txt`.
Scope: the root filesystem layout of the operating system that FNX will be
the kernel of (referred to here as "the OS"; FNX itself stays POSIX/VFS
compatible underneath).

---

## 1. Why redesign

FNX currently boots a classic FHS-derived tree:

```
/  bin/  sbin/  usr/  etc/  var/  tmp/  dev/  proc/  mnt/  home/
```

That layout is not a design — it is sediment. `/bin` vs `/sbin` vs
`/usr/bin` vs `/usr/local/bin` split the *same kind of thing* (executables)
by boot-phase, by who maintains it, and by which vendor shipped it. `/lib`
and `/usr/lib` duplicate libraries; `/etc` mixes machine config with
distro state; `/var` re-introduces the writable half of `/usr` that was
never actually read-only. Every directory answers "who owns this file"
historically rather than "what is this for" logically.

The OS gets one chance to define its own hierarchy. This proposal replaces
the FHS tree with a small, self-describing, **stakeholder-based** layout:
a handful of top-level categories, each with one unambiguous meaning, so a
user can predict where anything lives without memorizing Unix folklore.

## 2. Goals

1. **One concept per top-level directory** — the reader of `/` can say
   what each entry is for without knowing boot order or vendor history.
2. **Executables, libraries, config, and variable data each have exactly
   one home** — no `/bin` vs `/sbin` vs `/usr/bin`, no `/lib` vs
   `/usr/lib`, no `/etc` vs `/usr/etc`.
3. **Static vs dynamic separation** — what the OS ships (read-only) is
   cleanly split from what changes at runtime (logs, caches, temp).
4. **Media-neutral devices** — hardware is discovered and named by bus and
   topology, not by which driver happened to register it.
5. **Clean break, by design** — the root namespace belongs to the OS alone:
   no legacy path aliases. Software runs on FNX by being *ported* to the new
   namespace, explicitly and mindfully — never by hiding the old one behind
   symlinks.

## 3. The proposed hierarchy

The layout below is the original design from `docs/reference/fsh-notes.txt`, argued
out here from first principles. It is **not** the hierarchy of the FHS or
of any existing OS — it borrows only the POSIX primitives (files,
directories, symlinks, permissions) that every tree is built from, plus
the passing lineage of systems that explored similar territory (e.g. the
notion of a system/third-party/user resource split, familiar from
classic macOS's `/System` + `/Library` and BeOS's `/boot`). The result
is FNX's own namespace, derived from the
question "what kinds of things does this OS contain, and who owns them?"

```
/
    Applications/                 user-facing programs
    Shared/                       third-party resources, shared by all apps
        Application Support/      third-party behavioural scripts/data
        Configuration/            third-party machine-wide settings
        Libraries/                third-party shared objects
        Fonts/
        Images/
        Sounds/
        Videos/
        Documentation/
    System/                       the OS itself (read-only by default)
        ESP/                      mounted EFI System Partition (kernel, kernel.conf, firmware)
        Configuration/            machine & OS settings
        Keychains/                machine secrets (the System keychain; Admin-gated)
        Devices/                  hardware, bus-organized
            Disk/
                AHCI/
                    Disk0/
                        Partition0
                        Partition1
                        Partition2
            USB/
                Keyboard
                Mouse
        Documentation/
            HTML/
                FNX/
                    index.html
            PDF/
                FNX/
                    FNX.pdf
        Libraries/                shared objects (libc.so, libz.so, ...)
        Processes/                ${PID}          (kernel-published, like procfs)
        Shared/                       OS-owned resources (fonts, images, sounds)
            Fonts/
            Images/
                Icons/
                Wallpaper/
            Sounds/
            Videos/
        Source Code/              headers, sources, build recipes
        Temporary Files/          /tmp equivalent
        Tools/                    executables (system & admin)
        User Template/            home structure, copied to each new user
                                 (the /etc/skel idea, familiar from
                                 macOS's /System/Library/User Template)
        Variable Data/            /var equivalent (logs, spool, caches)
        Application Support/      system behavioural scripts/data (policy,
                                 config-design §0)
    Users/
        Admin/                    uid 0 (same structure as $USER)
        $USER/
            Application Support/   this user's behavioural scripts/data
            Configuration/        per-user settings
            Keychains/            this user's keychains (encrypted items)
            Applications/         per-user installed apps
            Documents/            the user's own files
            Desktop/              the user's desktop surface
            Music/                the user's music
            Pictures/             the user's pictures
            Videos/               the user's videos
            Shared/               resources this user installed
                Libraries/        per-user shared objects
                Fonts/
                Images/
                Sounds/
                Videos/
                Documentation/
            Temporary Files/      per-user scratch
            Variable Data/        per-user caches, spool
    Volumes/                      mount points for disks & media
```

### 3.1 Why these top-level entries

| Entry | Answers "what is this?" | Replaces |
|---|---|---|
| `Applications/` | things a person runs on purpose | (spread across bin/usr/bin today) |
| `Shared/` | resources installed by third parties, shared by all apps | third-party halves of `/usr/local`, `/opt`, shared `/usr/share` data |
| `System/` | the OS itself: programs, libs, config, devices, docs, state | bin, sbin, usr, lib, etc, var, dev, proc, tmp, boot |
| `Users/` | per-person home directories | home/ |
| `Volumes/` | anything mounted after boot | mnt/, and root-mounted disks |

### 3.2 Why these subdirectories under System/

- **Tools/** — *one* home for every executable, from `init` to `ls` to
  `dhcp`. Boot phase is irrelevant to the reader; an embedded OS simply
  has fewer tools in the same place. (Optional: a `Tools/Admin/` child if
  privilege separation ever matters — not a `/sbin` reincarnation.)
- **Libraries/** — all shared objects, flat. `libc.so`, `libz.so`, ...
  The dynamic loader lives here too (see §6).
- **Configuration/** — all machine settings, one place. No `/etc` split
  between rc scripts, passwd, fstab-analog, and per-daemon dirs.
- **Keychains/** — machine secrets (the **System keychain**), Admin-gated;
  each user's keychains live in their own `Users/$USER/Keychains/`.
  Deliberately not under `Configuration/` (these are encrypted blobs,
  not human-editable settings) and not under `Variable Data/` (they
  are not disposable state). Design: docs/design/keychain-plan.md.
- **Devices/** — a **bus/topology tree** instead of a flat `/dev`. The
  notes show the two regimes FNX already has: controller-oriented disks
  (`Disk/AHCI/Disk0/Partition0`) and class-oriented input
  (`USB/Keyboard`, `USB/Mouse`). Decided: the topology tree is the real
  namespace; role views (`Devices/Keyboard`, `Devices/Mouse`,
  `Devices/Serial/Port0`) are alias dirs inside it — the same pattern
  devfs uses today for `/dev/mouse` and `/dev/disk/by-id` (see §8, Q3).
- **Processes/** — the procfs analog, kernel-mounted. `${PID}` per
  process, read-only by default. A *visible* home for "where are my
  processes" instead of a hidden virtual fs.
- **Shared/** — OS-owned immutable, packaged data that is neither code
  nor docs: fonts, icons, wallpapers, sounds. Deliberate parallel with the
  top-level `/Shared` (third-party): same category of content, different
  origin — `System/Shared` is what the OS ships, `/Shared` is what third
  parties install. (A wallpaper is not a library, and it shouldn't live in
  `/usr/share`.)
- **Documentation/** — system manuals, organized by format
  (`HTML/FNX/`, `PDF/FNX/`). This is a **mounted documentation volume**,
  not a doc dir buried under `/usr/share`.
- **Source Code/** — headers + sources for what FNX ships, so the OS is
  self-hosting on disk, keeping the educational mission of FNX on the box
  itself.
- **Temporary Files/** — the `TMPDIR` for the whole OS. Explicitly
  ephemeral, world-writable, cleared at boot.
- **Variable Data/** — the only writable-*by-design* state under System/:
  logs, spool, caches, lock state. This is the clean "dynamic half" that
  `/var` was always trying to be.

### 3.3 Design rules that fall out

- **Origin, not just scope.** The top level answers *who owns something*:
  `System` is OS-owned (read-only), `Shared` is third-party-owned
  (installed by package management, writable by `Users/Admin` only),
  `Applications` is per-app, `Users` is per-person, `Volumes` is media.
  Third-party content never lands in `System/` — that would blur "System
  = the OS".
- **Everything static is under `/System` or `/Shared` and is conceptually
  read-only.** Runtime mutation is confined to `Variable Data/`,
  `Temporary Files/`, and `Users/`. Decided (Q4): read-only is **by
  convention first** — root-owned permissions, admin-writable — with
  VFS-enforced `MS_RDONLY` deferred to the hardening milestone. VFS
  enforcement cannot be a flat mount anyway: `Variable Data/` and
  `Temporary Files/` must stay writable, so it only becomes possible once
  they are separate sub-mounts (writable `Variable Data`, tmpfs
  `Temporary Files`) and `/Shared` is a separate admin-mountable tree.
- **No directory is split by boot phase, vendor, or privilege level.**
- **Capitalization is part of the design language**: `System` is the OS,
  `Applications` is software, `Users` is people. Decided: paths stay
  **case-sensitive** (FNX stays POSIX); the capitals are a naming
  convention, not a case-insensitive filesystem.

## 4. Mapping the current tree

| Today | New home |
|---|---|
| `/bin/*`, `/sbin/*`, `/usr/bin/*`, `/usr/sbin/*` | `/System/Tools/` |
| `/lib/*`, `/usr/lib/*` | `/System/Libraries/` |
| `/etc/*` | `/System/Configuration/` |
| `/etc/skel` | `/System/User Template/` |
| `/var/*` | `/System/Variable Data/` |
| `/tmp` | `/System/Temporary Files/` |
| `/dev` | `/System/Devices/` (flat node list → bus/topology tree) |
| `/proc` | `/System/Processes/` |
| `/home/*` | `/Users/*` |
| `/mnt/*`, root-mounted disks | `/Volumes/*` |
| `/usr/local/*`, `/opt/*`, third-party `/usr/share/*` | `/Shared/*` |
| `/usr/share/{doc,man,icons,...}` | `/System/Documentation/`, `/System/Shared/` |
| `/usr/include`, `/usr/src` | `/System/Source Code/` |
| `/boot` (kernel, initrd, firmware) | `/System/ESP/` — the EFI System Partition, mounted in the tree (no initrd — removed with the release) |

## 5. Kernel impact (what actually changes in FNX)

Ground truth from the current code:

- `include/fnx/kernel.h`: `INIT_PROGRAM "/sbin/init"` → `/System/Tools/init`.
- `kernel/init.c`: `init_console_dev "/dev/console"` →
  `/System/Devices/console`.
- `fs/devfs/super.c`: devfs mount point `/dev` → `/System/Devices`.
  The devfs registry is the biggest chunk of work: nodes become a
  hierarchical tree (bus → device → port/partition) instead of the flat
  list + alias dirs (`/dev/disk/by-id`).
- procfs mount point `/proc` → `/System/Processes`.
- `kernel/multiboot.c`: the fixed `/dev/tty0..12`, `/dev/ttyS0..3`,
  `/dev/ram0`, `/dev/hd*`, `/dev/sd*`, `/dev/nvme0n1` name tables are
  regenerated inside the new Devices/ topology tree, plus the role-alias
  views (`by-role`) so `console=` keeps working by role not by name
  (decided, §8 Q3).
- ESP mount: the EFI System Partition, which UEFI used to load the
  kernel, is mounted at `/System/ESP/` so the kernel image, its
  `kernel.conf`, and firmware are visible in the tree (decided, §8
  Q2). No initrd — the kernel boots the AGFS root directly (§9.2 Q8).
- `tools/mkinitrd.py` is **removed with the initrd**; the root tree is
  staged directly at the new paths (Makefile `ROOTFS64` staging,
  `userland64`/`rootdisk64` targets become a AGFS root build).
- devpts (UNIX98 ptys) mount point follows `/dev` → `/System/Devices/pts/`.
- `root=` kernel cmdline probing is *unaffected* — it addresses disks, not
  paths. `console=` keeps working if the Devices tree exposes a
  role-based view (e.g. `Devices/Serial/Port0` + `console=` resolver).

The VFS layer, syscalls (`open`, `chdir`, `getcwd`, `chroot`, ...),
filesystems (AGFS/ISO9660), and the procfs *content* all stay
put — only the mount points and the boot-time path constants move. That is
the entire kernel-side price of the redesign.

## 6. Compatibility: a clean break (decided)

**Decision: no Unix cruft in `/`.** The root of the OS contains exactly
the five top-level entries of §3 — `Applications/`, `Shared/`, `System/`,
`Users/`, `Volumes/` — and nothing else. There are no `/bin`, `/sbin`,
`/usr`, `/etc`, `/lib`, `/var`, `/tmp`, `/dev`, `/proc`, `/home`, or
`/mnt` symlinks (or any other alias) at the root. The alternatives were
considered and rejected:

- **Root-level alias symlinks** were the tempting option: a dozen links
  would let every hardcoded Unix path keep working. Rejected because each
  alias is a lie about where content actually lives — `find`, `stat`, `df`
  and friends report through them, applications accumulate dependence on
  them, and the cruft becomes permanent. The "radical" redesign would just
  be FHS with nicer lighting.
- **Kernel namespace remap** (VFS rewrites legacy prefixes internally) was
  rejected: magic in every path walk, breaks tools that stat the real
  tree, and is the most code for the least gain.

Consequence: **every piece of software is explicitly and mindfully
ported.** That is a feature, not a cost — the point of the redesign is
that the OS's namespace is its own, and software that runs on it has been
*made* to run on it.

### 6.1 What "mindfully ported" means in practice

- **Toolchain, not sed.** The FNX toolchain (`tools/musl-gcc64.sh` + the
  musl build) is configured with the new paths baked in: musl's config
  paths (`passwd`, `group`, ...) point at `/System/Configuration/`, its
  syslibdir at `/System/Libraries/` (the loader search path spans
  `System/Libraries` + `Shared/Libraries`), `TMPDIR` at
  `/System/Temporary Files/`.
  Today's userland is static-only — no dynamic linker yet,
  `musl-gcc64.sh` forces `-static` — so there is *nothing* to fix right
  now. When dynamic linking arrives, the linker generates `PT_INTERP` from
  the toolchain's syslibdir, so the loader path is correct at link time,
  with no kernel intervention.
- **A porting checklist per app**: shebangs rewritten to
  `#!/System/Tools/...`; hardcoded `/etc`, `/var`, `/tmp`, `/usr`
  references replaced per the §4 mapping; build recipes gain an FNX port
  target. The kernel honors `PT_INTERP` verbatim — a ported binary carries
  the correct path by construction.
- **Enforced by the build (decided).** The porting gate is mechanical, so
  it is a linter in the packaging step: scan each staged ELF for
  `PT_INTERP` and for embedded path strings that start with a legacy
  prefix (`/bin`, `/sbin`, `/usr`, `/etc`, `/lib`, `/var`, `/tmp`,
  `/dev`, `/proc`, `/home`, `/mnt`); any hit fails the image build.
  Unported software cannot silently ship.
- **What FNX keeps promising is source compatibility**: the POSIX API
  surface stays — that is the kernel's job, unchanged by this proposal.
  Path compatibility is deliberately dropped; the porting bar is "rebuild
  against the FNX toolchain, then fix the path assumptions", which is
  exactly the mindful porting this proposal requires.

## 7. Phased rollout

1. **Mount-point move (kernel)**: relocate devfs and procfs to
   `/System/Devices` and `/System/Processes`; update `INIT_PROGRAM`,
   `init_console_dev`, and the multiboot device tables. The root has no
   compatibility entries — nothing but the five top-level directories.
2. **Toolchain + staging rebuild (userland)**: bake the new paths into the
   FNX musl toolchain; restructure the `rootfs64` staging tree
   (`bin/ sbin/ usr/ etc/ var/ tmp/` → `System/{Tools,Libraries,
   Configuration,Variable Data,Temporary Files}`); build the AGFS root
   staging (Makefile `ROOTFS64`/`rootdisk64` targets; `mkinitrd.py` is
   removed with the initrd). Devfs registry becomes hierarchical.
3. **Port the userland + enforce**: port `init` and the toybox applets to
   the new namespace; add the legacy-path linter to the packaging step so
   unported binaries fail the image build.
4. **Applications/Shared/Volumes (userland policy)**: `init` mounts disks
   under `/Volumes`, launches apps from `/Applications`, creates `/Shared`
   (admin-writable) for third-party installs, and on first boot creates
   `/Users/Admin` (uid 0) and `/Users/$USER` by copying
   `/System/User Template/` (the macOS `/System/Library/User Template`
   convention).
5. **Docs/resources (packaging)**: move docs and resources into
   `/System/Documentation` and `/System/Shared`; FNX.pdf ships on the
   root image.

Each phase boots on its own and is reversible.

## 8. Decisions

- **Q1 — Porting enforcement: linter gate in the build.** The §6.1 linter
  scans staged ELFs for `PT_INTERP` and embedded legacy path strings;
  unported binaries fail the image build.
- **Q2 — Kernel: the ESP, mounted at `/System/ESP/`.** The EFI System
  Partition is mounted in the tree at `/System/ESP/`; the kernel image,
  its `kernel.conf` (kernel boot options, `.conf` format), and firmware
  live there. (UEFI loads the kernel from the ESP at boot; the kernel
  then mounts it so they are visible. No initrd — removed with the
  release.)
- **Q3 — Device tree: topology + role-alias dirs.** `System/Devices/` is
  the real topology namespace (`Disk/AHCI/Disk0/Partition0`); role views
  (`Keyboard`, `Mouse`, `Serial/Port0`) are alias dirs inside it — the
  same pattern devfs uses today for `/dev/mouse` and `/dev/disk/by-id`.
- **Q4 — `/System` read-only: by convention, VFS-enforced later.**
  Convention first — root-owned permissions, admin-writable. VFS
  `MS_RDONLY` enforcement is deferred to the hardening milestone because
  a flat read-only `/System` would block `Variable Data/` and
  `Temporary Files/`; it becomes possible once they are sub-mounts and
  `/Shared` is a separate admin-mountable tree (see §3.3).
- **Q5 — Case-sensitive paths.** POSIX behavior preserved; the capitals in
  the design language are convention only, not a case-insensitive fs.
- **Q6 — `Admin` is a real per-user home.** uid 0, the first user created
  at first boot, home `/Users/Admin` holding the admin's config — keeping
  the "`Users/` = per-person homes" invariant exception-free.

Questions Q1–Q6 are resolved in §8; six further design areas raised
during review are resolved in §9 (Q7–Q12). The proposal is fully decided.

---

## 9. Additional decisions (Q7–Q12)

Six further design areas surfaced during review and are all resolved here.

### 9.1 Q7 — Naming + filesystem policy — DECIDED

**Spaced phrases verbatim; AGFS is the root filesystem.**

- Naming: the notes' names are kept exactly — `Variable Data`,
  `Temporary Files`, `Source Code` — spaces and all. Shell quoting is
  accepted as part of the design. There is **no initrd constraint**: the
  whole tree lives on the AGFS root, where 255-char names fit spaced
  phrases comfortably. `Single-token CamelCase` and `hyphenated` were
  considered and rejected: the former loses the human-readable
  phrases, the latter adds a third naming style.
- Root filesystem: **AGFS** — the native writable filesystem.
  ext2 and minix support are removed as part of the release. The
  supported-filesystem roster: **AGFS** (native, system volumes),
  **FAT32** and **ExFAT** (compatibility and removable media — the ESP
  is FAT32), **ISO9660** (read-only, install media). FAT32/ExFAT
  drivers also fill the ESP mount/write gap (Q2/Q8) and extend the
  Disks formatting set. The hierarchy itself stays fs-agnostic — this
  decision is about the deployment default, not a dependency.

### 9.2 Q8 — Boot flow — DECIDED

**No initrd — the kernel boots the AGFS root directly.** ext2, minix,
and initrd support are removed as part of the release. The boot
sequence:

1. Firmware loads `FNX.efi` from the ESP.
2. The kernel reads its options from `/System/ESP/kernel.conf`
   (next to the kernel, in the config `.conf` format — see
   `docs/design/config-design.md` §11), via early ESP access: the FAT32
   driver, or UEFI boot-services file I/O before `ExitBootServices`
   until then. The kernel cmdline, when present, overrides
   individual keys.
3. The kernel boots (long mode, page tables, drivers) and mounts the
   AGFS system root directly (`root=` from the kernel config).
4. The kernel mounts devfs at `/System/Devices`, procfs at
   `/System/Processes`, devpts under Devices, and the ESP at
   `/System/ESP` (Q2 — FAT32 driver, planned).
5. `init` runs from `/System/Tools/init`, mounts volumes under
   `/Volumes`, scaffolds `/Users/*` on first boot.

There is no boot initrd and no RAMdisk — one tree, mounted at boot.

### 9.3 Q9 — User-home structure + accounts — DECIDED

**Structured, self-similar homes.** A home is a per-person mini-tree
mirroring the writable categories of the top level, plus the person's own
content and per-user resources:

```
Users/$USER/
    Configuration/          per-user settings
    Applications/           per-user installed apps
    Documents/              the user's own files
    Desktop/                the user's desktop surface
    Music/                  the user's music
    Pictures/               the user's pictures
    Videos/                 the user's videos
    Shared/                 resources this user installed
        Libraries/          per-user shared objects
        Fonts/
        Images/
        Sounds/
        Videos/
        Documentation/
    Temporary Files/        per-user scratch
    Variable Data/          per-user caches, mail spool
```

Read-only system categories (`Tools`, `Libraries`, `Devices`, ...) need no
per-user copy — the invariant "same name, same meaning" holds at every
level, and a home is nothing but "the part of the tree that is mine". The
origin model is now complete at every level: `System/Shared` is what the
OS ships, `/Shared` is what third parties install, and
`Users/$USER/Shared` is what the person installs for themselves (a regular
user cannot write the admin-only `/Shared`). `Users/$USER/Shared` mirrors
`/Shared`'s structure exactly — `Libraries/`, `Fonts/`, `Images/`,
`Sounds/`, `Videos/`, `Documentation/` — so "same name, same meaning"
holds there too. `Documents/`, `Desktop/`, `Music/`, `Pictures/`, and
`Videos/` cover the category the top level doesn't have — personal
content — because the top level is ownership-based, not content-based;
`Desktop/` is additionally the GUI's file surface.
Free-form homes were considered and rejected: they lose the
self-similarity and predictability of the design language.

Accounts: `Admin` is uid 0 (Q6). `passwd`/`group` live in
`System/Configuration/` (already in the §4 mapping); user management is a
Tool (`useradd` analog) that writes Configuration and creates the home
tree by **recursively copying `/System/User Template/`** (the classic
`/etc/skel` idea, in the manner of macOS's `/System/Library/User
Template`); `init` copies
the template for `/Users/Admin` and the first user on first boot
(§7 phase 4). The template holds the standard home structure — the
empty per-user directories (Configuration/, Applications/, Documents/,
Desktop/, Music/, Pictures/, Videos/, Shared/{Libraries,Fonts,Images,
Sounds,Videos,Documentation}, Temporary Files/, Variable Data/) plus any
default files — so every new home is identical by construction.

### 9.4 Q10 — Volumes policy — DECIDED

**Label-first naming; boot-time system mounts now, hotplug daemon later.**

- **Naming.** Mount points under `/Volumes` use the filesystem label when
  the fs carries one (ext2 label, ISO9660 volume id), with a
  topology-derived fallback otherwise (`/Volumes/Disk0-Partition0`). This
  needs fs-metadata read support for the label. Alternatives (always
  topology, always label) were considered and rejected: topology-only is
  deterministic but ugly, label-only breaks on unlabeled media.
- **Mount policy.** `init` mounts system volumes at boot; the
  mount-on-hotplug daemon for removable media is a separate later
  milestone — the kernel already emits USB hotplug events, but no userland
  mount daemon exists yet. The system root itself is not a Volume;
  `/Volumes` is for additional disks and media.

### 9.5 Q11 — Risks + acceptance criteria — DECIDED

**The acceptance criteria are binding gates: a §7 phase is "done" only
when all of them pass.**

Risks, with mitigations:

- **Devfs registry rework** (flat list → hierarchy, §5) is the largest
  kernel change. Mitigation: keep the flat list as an internal layer and
  build the hierarchy view over it incrementally — the existing
  `/dev/mouse` + `/dev/disk/by-id` alias mechanism is the seed (Q3).
- **Boot-path constant changes** (`INIT_PROGRAM`, `init_console_dev`,
  mount points) can silently break boot. Mitigation: every §7 phase boots
  standalone (already a rollout rule) behind a golden boot test (QEMU run
  to shell prompt).
- **Porting gaps** in `init`/toybox applets. Mitigation: the linter gate
  (§6.1) + the per-app checklist.
- **FAT32/ExFAT (Q2/Q8)** — the ESP mount at `/System/ESP` and the
  Installer's boot-setup write path are filled by the planned FAT32
  driver; ExFAT covers large removable media. Both drivers do
  UTF-16 ↔ UTF-8 long-filename conversion per the UTF-8-only policy.

Acceptance criteria (binding gates, per phase):

- `ls -a /` returns exactly the five top-level entries, period.
- Every staged ELF passes the linter (build gate, Q1).
- Boot-to-shell on the new tree, with `test ! -e /bin` etc. proving no
  legacy paths exist at runtime.
- All current functionality (init → shell, mount, network, USB) still
  passes after each phase.

### 9.6 Q12 — Runtime environment contract — DECIDED

**Adopted, with a per-user PATH element from day one.** Every process
inherits:

- `PATH=/System/Tools:/Users/$USER/Applications` — one system tool
  directory, plus the per-user executable home from Q9 (per-user
  installed apps in `Users/$USER/Applications`); no `/sbin` split.
- `HOME=/Users/$USER` (or `/Users/Admin`), `SHELL=/System/Tools/sh`,
  `TMPDIR=/System/Temporary Files`.
- Loader search: `System/Libraries` + `Shared/Libraries` (§6.1).
- Config lookup: `System/Configuration` (`passwd`, `group`, `hosts`, ...).
- Temp defaulting to `/System/Temporary Files` when `TMPDIR` is unset.

---

*This draft is grounded in the current FNX tree: the hardcoded paths and
mount points in §5 are taken from `include/fnx/kernel.h`,
`kernel/init.c`, `kernel/multiboot.c`, `fs/devfs/super.c`, and
`tools/mkinitrd.py`.*
