# docs/design — the status of every plan

**Measured 2026-09-20.** One row per document. **Every column is a fact taken from
the tree or from git, and none of them is a verdict** — the reason is in the last
section, and the honest shortlist is there too.

| column | what it MEASURES |
|---|---|
| **its own status** | the document's own `Status:` line, verbatim (truncated). What it says about itself — which is not the same as whether its subject is real. |
| **what it is** | the document's own H1. |
| **path evidence** | the `userland/ kernel/ drivers/ fs/ net/ mm/ tools/ tests/` paths the doc names. `on disk` = one exists today; `in history only` = the paths it names once existed and git says they are gone. |
| **park tag** | whether the document names a `park/*` ref — work that is recoverable rather than lost. |
| **cited by** | how many OTHER documents here reference it by filename. **This is the load-bearing measure**: a doc cited by a live plan must not be filed away for tidiness. |

## The five status classes (naming what the headers already mean)

`docs/README.md` says every document records its own status in its header. This is
the finite vocabulary those headers should draw from, so the set stays readable —
the directory currently spells the same ideas fifteen ways:

| class | words | meaning |
|---|---|---|
| **settled** | `DECIDED` `CHOSEN` `APPROVED` | a decision, and the decision is the record |
| **open** | `PLAN` `PROPOSED` `DRAFT` | a live intention, not yet built |
| **shipped** | `DONE` `COMPLETE` | built, and recorded as such |
| **not-now** | `DEFERRED` `PARKED` `STOWED` | not abandoned, not being worked on |
| **not-happening** | `RETRACTED` `REJECTED` `DISCARDED` | reversed, with the cause recorded |

## WHY THERE IS NO "PARKED" COLUMN

Three mechanical tests were tried for one and all three mislabel, which is worth
recording so nobody tries a fourth: *"the paths it names do not exist"* puts a plan
for **unbuilt** work in with a plan whose subject was **removed**;
*"a path it names is in git history"* catches a document that merely **cites** a
file later deleted; and *"a path it names exists"* catches a document whose
**container** directory survives (the UIKit plans name `userland/tests/`, which is
there, and `userland/argentum/`, which is not). **A machine can report these facts;
it cannot tell you what a document is about.**

## THE SHORTLIST, AS A JUDGEMENT — mine, 2026-09-20

What the columns support, read by hand: **the Argentum UIKit generation is parked,
not abandoned, and its plans are the records.** `argentum-hig`, `argentum-s21`,
`argentum-s22`, `argentum-s23`, `argentum-s3-input-depth`, `argentum-textview`,
`argentum-uikit-catalog`, `argentum-uikit-plan`, `cocoa-parity-plan`,
`uikit-documentation-plan` — all of them, plus everything they described, are
recoverable at **`park/argentum-uikit-u6a`** (branch `park/argentum-uikit`,
`1fdf92f4`; 59 files, ~19.4k lines). `kestrel-compositor-plan` and
`argentum-s4-kestrel` record a removed window manager. `corefoundation-plan` is
**retracted** (see its own header). Everything else in this directory has a
subject that is present, planned, or is a record with no subject at all.

**Regenerate** by re-running the same derivations — the `Status:` line, the path
existence test, the `git log --all -- <path>` test, and the citation count. A
documented recipe rather than a gate: this tree's rule is no new test tooling, and
83 rows do not need one.

---

### **NAMES PATHS, NONE ON DISK** — the document's own status, and whether the paths it names ever existed. **This is not a verdict**: a plan may cite a path removed for reasons unrelated to its own subject (`gpu-accel-plan.md` cites `drivers/video/ati.c`; GPU acceleration is untouched), and a doc whose subject IS gone may still name a container path that survives.  (9 docs)

| doc | its own status | what it is | path evidence | park tag | cited by |
|---|---|---|---|---|---|
| `argentum-hig.md` | DECIDED (2026-09). These are conventions application | Argentum Interface Guidelines (HIG) | in history only: `tests/cases/wm_dock.py` | — | 4 |
| `argentum-s21-view-tree.md` | DRAFT (2026-09). Design for S2.1 of | Argentum S2.1 — the View tree (L2 keystone): des | in history only: `userland/argentum/argentum.h` | — | 3 |
| `argentum-s22-control-first-leaves.md` | DRAFT (2026-09). Design + split for S2.2 of | Argentum S2.2 — Control + first leaves (plan L4) | in history only: `userland/argentum/control.cpp` | — | 2 |
| `argentum-s23-tier1-rest.md` | DRAFT (2026-09). Design + split for S2.3 of | Argentum S2.3 — rest of Tier 1 (plan L5) | in history only: `userland/argentum/slider.cpp` | — | 2 |
| `argentum-s3-input-depth.md` | DRAFT (2026-09). Design + split for S3 of | Argentum S3 — Input & text depth | in history only: `userland/argentum/window.cpp` | — | 1 |
| `argentum-textview.md` | DRAFT (2026-09). Design + split for the `TextView` — | Argentum TextView (L7 rich view) | in history only: `userland/tests/textview_a.cpp` | — | 1 |
| `argentum-uikit-catalog.md` | DECIDED (2026-09). The Argentum view catalog target  | Argentum catalog — Snow Leopard-parallel view in | in history only: `userland/argentum/` | — | 8 |
| `gpu-accel-plan.md` | PLAN (2026-09) — decided in direction; not scheduled | GPU acceleration — the engine framework (nouveau | in history only: `drivers/video/ati.c` | — | 1 |
| `uikit-documentation-plan.md` | PLAN (2026-09) — decided in direction; not scheduled | UIKit documentation — programmer-facing referenc | in history only: `tools/uikitdoc.py` | — | 2 |

### **AT LEAST ONE NAMED PATH IS ON DISK**  (46 docs)

| doc | its own status | what it is | path evidence | park tag | cited by |
|---|---|---|---|---|---|
| `agfs-enhancements.md` |  | AGFS enhancements | on disk: `fs/agfs` | — | 0 |
| `argentum-milestone-split.md` | DRAFT (2026-09). Breaks the coarse S0–S5 milestones  | Argentum milestone split — small, individually v | on disk: `tools/config_m2_test.sh` | — | 7 |
| `argentum-rebrand-plan.md` | PLAN — for execution, not started. Governs the monob | Argentum rebrand — full rename plan | on disk: `fs/agfs/` | — | 0 |
| `argentum-s24-tier2-structure.md` | DRAFT (2026-09). Design + split for S2.4 of | Argentum S2.4 — Tier 2 structure (plan L6) | on disk: `userland/tests/xclick.c` | — | 0 |
| `argentum-s4-kestrel.md` | DRAFT (2026-09). Design + split for S4 of | Argentum S4 — Kestrel (window manager + global m | on disk: `kernel/boot64/irq64.c` | — | 3 |
| `argentum-uikit-plan.md` | DEFERRED (2026-09-17) — the user's call: park the C+ | Argentum UIKit — DEFERRED (parked 2026-09-17) | on disk: `tools/x11-shared-build.sh` | yes | 16 |
| `ati-nvidia-fb-plan.md` | PLAN (draft for review; nothing implemented). | Generic native framebuffer drivers for ATI + NVI | on disk: `drivers/char/fb.c` | — | 2 |
| `bundle-launch-plan.md` | DECIDED (2026-09) — mechanism unimplemented. | Bundle launch — the sanctioned door and exec aut | on disk: `kernel/syscalls/execve.c` | — | 8 |
| `cocoa-parity-plan.md` | DRAFT (2026-09), for review — AND ONE OF ITS PREMISE | Cocoa parity for the Argentum UIKit — plan | on disk: `mm/mmap.c` | yes | 7 |
| `config-design.md` | DRAFT — design decisions locked; no open questions. | `config` — a universal configuration utility | on disk: `userland/libconfig.c` | — | 9 |
| `config-v2-plan.md` | M0–M2 DONE (bb5c394, 74985f5, d8fc0b1/f486a9a); M3 c | libconfig v2 — tree values, arrays of records, a | on disk: `tools/kconf_corpus.sh` | — | 1 |
| `corefoundation-plan.md` | RETRACTED, by the user's direction. Do not start thi | CoreFoundation — RETRACTED (2026-09): this tree  | on disk: `tools/coregraphics-sweep.py` | — | 1 |
| `coregraphics-plan.md` | PLAN (2026-09) — decided in direction; not scheduled | CoreGraphics — duplicating Apple's drawing API | on disk: `tools/coregraphics-sweep.py` | yes | 2 |
| `cpp-toolchain-plan.md` | PLAN — for execution. Adds C++ to the FNX native x86 | LLVM C++ runtime + C++ capability plan (via the  | on disk: `tools/fetch-llvm.sh` | — | 4 |
| `fatfs-driver-plan.md` | M1 DONE (FAT32 read + write) — M0 = c76bd44, M1 = <M | Native FAT12/16/32 + exFAT driver (fs/fatfs) | on disk: `drivers/block/ata_hd.c` | — | 0 |
| `finch-shell-plan.md` | PLAN (2026-09) — from-scratch, fully designed, not y | Finch shell plan | on disk: `drivers/char/tty.c` | — | 0 |
| `fontconfig-config-plan.md` | M0 DONE (committed): default config loads from the l | fontconfig configuration → libconfig domain plan | on disk: `tools/x11-shared-build.sh` | — | 3 |
| `foundation-plan.md` | DRAFT (2026-09). F0–F4 and F6–F12 LANDED, the audite | The Foundation (Argentum Foundation) — plan for  | on disk: `fs/agfs/` | — | 6 |
| `fsh-proposal.md` | DRAFT — for discussion. Source notes: `docs/referenc | Proposal: a radical redesign of the FNX filesyst | on disk: `fs/devfs/super.c` | — | 9 |
| `kernel-conf-plan.md` | DONE (2026-09) — M0..M3 complete. M0 (EFI stub reade | kernel.conf — ESP boot config implementation pla | on disk: `kernel/boot64/efi_stub.c` | — | 1 |
| `kestrel-compositor-plan.md` | PROPOSED (2026-09). Decision requested before code s | Kestrel as a compositing WM — plan | on disk: `tests/cases/` | — | 1 |
| `llvm-clang-toolchain-plan.md` | PLAN (2026-09) — design decided, nothing implemented | LLVM/Clang toolchain plan — one compiler for ker | on disk: `drivers/net/virtio_net.c` | — | 2 |
| `mit-shm-plan.md` | PLAN (2026-09) — server half already present; M0 (pr | MIT-SHM on Xfb — zero-copy client→server image t | on disk: `tools/x11-shared-build.sh` | — | 1 |
| `native-input-plan.md` | N0 + N1 DONE (2026-09); N2 (keyboard) next. | Native input devices — real HID delivery, no PS/ | on disk: `drivers/char/kbdaux.c` | — | 2 |
| `objc-toolchain-plan.md` | DRAFT (2026-09), for review. P1 PASSED on 2026-09-17 | Objective-C for the Argentum OS — plan (clang fr | on disk: `tests/COVERAGE.md` | yes | 3 |
| `package-format-plan.md` | PLAN (2026-09) — decided in direction; not scheduled | Native package format — AGFS volume images | on disk: `drivers/block/` | — | 2 |
| `permissions-acl.md` | DECIDED — fully decided (Q-P1..Q-P4, §6). POSIX | Permissions — POSIX ACLs as the single canonical | on disk: `fs/agfs/xattr.c` | — | 3 |
| `plist-config-plan.md` | P3a and P3b DONE (2026-09-17: the core keeps comment | Converting FNX config to XML plists — plan | on disk: `kernel/kconf.c` | — | 2 |
| `sdl-plan.md` | PLAN (2026-09) — decided in direction; not scheduled | SDL as the third-party graphics/audio door | on disk: `tools/x11-shared-build.sh` | — | 0 |
| `security-hardening-plan.md` | PLAN (2026-09) — decided in direction; not scheduled | Security hardening | on disk: `tools/sec_test.c` | — | 3 |
| `self-hosting-packages.md` | REFERENCE — the living list of what FNX must ship to | Self-hosting package manifest (FNX) | on disk: `tests/cases/foundation_core.py` | — | 12 |
| `shared-libraries-plan.md` | DECIDED (design, 2026-09) + M0-M4 DONE — dynamic wor | Shared libraries on FNX | on disk: `drivers/char/tty.c` | — | 6 |
| `smp-plan.md` | PLAN — for execution, not scheduled. Turns the verif | SMP plan — multiple CPUs and cores for FNX | on disk: `drivers/pci/msix.c` | — | 0 |
| `sterling-plan.md` | DRAFT (2026-09), for review. Nothing implemented. Th | Sterling — a modern syntax over Objective-C, for | on disk: `tests/cases/objc_smoke.py` | — | 1 |
| `sterling-syntax.md` | DRAFT (2026-09), for review. The surface only; no co | Sterling syntax — a modern surface over Objectiv | on disk: `tools/musl-clang-objc64.sh` | — | 2 |
| `swap-plan.md` | PLAN (2026-09) — decided in direction; no code. Scop | Swap support (plain, under-pressure) | on disk: `mm/swapper.c` | — | 0 |
| `system-admin-principal.md` | DECIDED in direction (design discussion, 2026); reso | The System / Service / Admin principal model — d | on disk: `kernel/syscalls.c` | — | 5 |
| `system-config-files-plan.md` | DRAFT — awaiting review. Companion to `docs/design/c | System config files → libconfig domains plan | on disk: `fs/agfs/namei.c` | — | 2 |
| `system-extensibility.md` | CHOSEN — fully decided (Q-X1..Q-X7, §8). Classic Mac | System extensibility — how FNX answers what clas | on disk: `drivers/pci/pci.c` | — | 2 |
| `toybox-fsh-plan.md` | PLAN — for later execution. Applies to the pinned to | Toybox → FSH port plan | on disk: `tools/mktoybox.sh` | — | 1 |
| `udf-filesystem-plan.md` | PLAN (2026-09) — scoped; no code. Adds UDF | UDF filesystem support | on disk: `fs/filesystems.c` | — | 0 |
| `workspace-plan.md` | DEFERRED (2026-09). The user's call, verbatim: "This | Workspace — the file manager (Miller-column brow | on disk: `userland/icons/lucide/` | — | 6 |
| `x11-xvfb-fb-plan.md` | PROPOSED (2026-09). Decision requested before code s | x11-xvfb-fb-plan — a real X11 server on FNX via  | on disk: `tools/lv_gui_test.py` | — | 3 |
| `xfb-accel-plan.md` | PLAN (2026-09) — decided in direction; not scheduled | Xfb acceleration — what a 2D engine changes in t | on disk: `drivers/char/fb.c` | — | 2 |
| `xfb-input-stall-finding.md` | CLOSED (2026-09). Root cause found and fixed in the  | Xfb: pointer input stalls after a fast burst (RE | on disk: `drivers/char/mousedev.c` | — | 0 |
| `xfb-shadow-buffer-plan.md` | DONE (S0-S2, 2026-09) — shadow draw + damage-driven | Xfb server shadow buffer — frame-atomic software | on disk: `userland/xfb/damageext` | — | 2 |

### **NAMES NO PATHS** — a design record, a spec or a language doc; there is no subject to be present or absent  (28 docs)

| doc | its own status | what it is | path evidence | park tag | cited by |
|---|---|---|---|---|---|
| `app-model.md` | CHOSEN — fully decided (Q-A..Q-E, §7). | Application model — app bundles | — | — | 6 |
| `audio-mixer-plan.md` | APPROVED (2026-09) — implementation deferred; not | Audio mixer — one device, many streams | — | — | 1 |
| `bluetooth-plan.md` | PLAN (2026-09) — scoped as a phased map; no code. Bl | Bluetooth support | — | — | 0 |
| `bundle-signing-plan.md` | DECIDED POLICY (2026-09) — mechanism unimplemented. | Bundle signing — supported, never required | — | — | 7 |
| `cli-design.md` | PROPOSED (design riff, 2026); related to the System/ | FNX command-line design — the domain-verb suite | — | — | 0 |
| `clipboard-plan.md` | DECIDED (2026-09) — nothing implemented. | Clipboard — one session pasteboard | — | — | 1 |
| `image-libraries-plan.md` | PLAN (2026-09) — decided in direction; not scheduled | Image codec libraries — the decode/encode door | — | — | 0 |
| `initial-release.md` | CHOSEN — fully decided (Q-R1..Q-R4, §4). Scope for t | Initial release — application goals | — | — | 3 |
| `kernel-debugger-plan.md` | PLAN (2026-09) — decided in direction; no code until | Kernel/system debugger — the serial gdb stub | — | — | 0 |
| `keychain-plan.md` | PLAN (2026-09) — decided in direction; nothing imple | Keychain — system-wide, per-user secrets | — | — | 3 |
| `libressl-plan.md` | PLAN (2026-09) — decided in direction; not scheduled | LibreSSL as the system SSL library | — | — | 1 |
| `overlay-mounts-plan.md` | PLAN (2026-09) — decided in direction; no code. VFS- | Overlay mounts — stacked copy-up union | — | — | 0 |
| `pdf-generation-plan.md` | PLAN (2026-09) — decided in direction; not scheduled | PDF generation — libharu (the write side) | — | — | 2 |
| `pdfium-plan.md` | PLAN (2026-09) — decided in direction; not scheduled | PDFium — the PDF viewing door | — | — | 3 |
| `printing-plan.md` | PLAN (2026-09) — decided in direction; no code. FNX | Printing — the thin spooler shape (CUPS rejected | — | — | 1 |
| `self-hosting-plan.md` | PLAN — long-term vision. Nothing below is scheduled. | Self-hosting plan (long-term) | — | — | 3 |
| `service-management-plan.md` | PLAN (2026-09) — shaped in conversation; no code. Bu | Service management | — | — | 2 |
| `sessionmgr-design.md` | DESIGN RECORD (2026-09) — direction decided in conve | sessionmgr — the session/login architecture | — | yes | 7 |
| `software-raid-plan.md` | PLAN (2026-09) — decided in direction; no code. Scop | Software RAID (mdadm-shaped) in the kernel | — | — | 1 |
| `terminal-plan.md` | DECIDED (2026-09) — nothing vendored or implemented. | Terminal — libvterm core + first-party Argentum  | — | — | 3 |
| `usb-audio-plan.md` | PLAN (2026-09) — scoped; no code. USB headsets, spea | USB audio — UAC (USB Audio Class) support | — | — | 0 |
| `usb-hid-plan.md` | PLAN (2026-09) — its delivery seam is superseded by | More HID-class USB devices — a generic HID layer | — | — | 1 |
| `usb-midi-plan.md` | PLAN (2026-09) — scoped, small; no code. USB MIDI | USB MIDI — MIDIStreaming (MS) class support | — | — | 0 |
| `usb-printer-plan.md` | PLAN (2026-09) — scoped, small; no code. The USB pri | USB printer class support | — | — | 0 |
| `usb-serial-plan.md` | PLAN (2026-09) — scoped, small; no code. CDC-ACM | USB serial — CDC-ACM class support | — | — | 0 |
| `utf8-only.md` | DECISION — the OS supports UTF-8 as its single text  | Encoding policy: UTF-8 only (decided) | — | — | 1 |
| `uvc-camera-plan.md` | PLAN (2026-09) — scoped; no code. Every mainstream U | USB webcams — UVC (USB Video Class) support | — | — | 1 |
| `website-plan.md` | PLAN (2026-09) — decided in direction; not scheduled | Argentum website | — | — | 1 |
