# Converting FNX config to XML plists — plan

Status: **P3a in progress** (the core's comment support). Decisions taken
2026-09-17 by the user: format is **XML plist v1.0**; sharing is **one C core,
two skins** (see `docs/design/foundation-plan.md`); scope is **everything,
`kernel.conf` included**; and **comments are preserved** — the core keeps them.

## What exists today, measured

| thing | size | note |
|---|---|---|
| `userland/libconfig.c` | **4069 lines** | the home-grown parser, renderer and tree |
| `userland/tools/config.c` | 937 lines | the `config` CLI |
| `include/plist.h` + `userland/plist.c` | 450 + 1052 | the plist core and its skin (P1/P2, landed) |
| the shipped files | **10 `.conf`** | under `userland/configuration/` |
| `kernel/kconf.c` | 223 lines | the KERNEL's own parser for `kernel.conf` |
| `third_party/musl-pwconf.patch` | **987 lines** | libc's own reader for passwd/group |
| `third_party/musl-hosts.patch` | 319 lines | libc's own reader for hosts |

So the same files are parsed by **five** readers: libconfig, the `config` tool,
`init` (mounts), Xfb's `configargs`, **libc**, and the **kernel**. That is the
whole difficulty of this conversion, and it is why the order below is what it is.

**The files are mostly documentation.** `system.display.conf` is 53 comment lines
of 60; `system.fonts.conf` 25 of 28. Our plist writer emits no comments, so
without the core change below, the first `config set` would silently delete the
prose — including the header of `system.mounts.conf` that explains what `fstype`
and `source` mean.

## The rule that governs the order

**A file may only change format in the same commit that its last reader learns
the new one.** Converting a file first means the login system, name resolution or
the mount table stops working — the class of breakage that locks you out of your
own machine. Every stage below therefore converts a reader *before* the files it
reads, and the reader accepts **both** formats during the transition.

## Stages

### P3a — the core keeps comments (IN PROGRESS)
`plist_value_t` gains `PLIST_COMMENT`, holding the text between `<!--` and `-->`.
The parser **retains** comments as items instead of skipping them, and the
serialiser writes them back **in place** at the same indent.

Containment, because the tree's shape has to tolerate them:
* in an **array**, a comment is an item of type `PLIST_COMMENT`;
* in a **dictionary**, it occupies a slot whose key is **NULL** — the one
  sentinel in the pair arrays, so `plist_dictionary_get` and the count/iteration
  accessors skip it;
* the Objective-C skin **drops** comments, which is correct rather than a gap:
  Cocoa has no comment type, so `NSPropertyListSerialization` must not invent
  one. The asymmetry is deliberate — the C path (libconfig, the `config` tool)
  preserves comments, the Cocoa path does not, and each is documented where it
  matters.

Acceptance: a document with comments round-trips byte-identically through
parse→serialise, comments stay attached to the entry they precede, and the
existing 24-check core probe still passes unchanged.

### P3b — libconfig reads plists, and writes them
Reimplement `libconfig.c` on the core, **keeping its public API** (`libconfig.h`
is what `init`, the `config` tool, Xfb's `configargs` and toybox's account tools
use). During the transition it reads **both** the legacy syntax and XML plists,
and writes plists. Nothing converts yet, so this stage is invisible to the system
except for the writer.

Acceptance: every existing `.conf` parses identically through both readers —
asserted by loading each file both ways and comparing the resulting trees, which
is the only honest way to show a parser rewrite is faithful.

### P3c — convert the files libconfig owns
One domain per commit, each verified in the guest: `system.mounts.conf` (`init`
reads it at boot — the riskiest, do it first while the guest is being watched),
then `system.fonts.conf`, `fonts.conf`, `system.display.conf`, `system.xfb.conf`.
Comments become XML comments and are preserved.

`system.shells.conf` is decided here by which reader owns it: if libc serves
`getusershell`, it waits for P3d.

### P3d — musl parses plists
`musl-pwconf.patch` (987 lines) and `musl-hosts.patch` (319) rework their readers
onto a plist parse inside libc. Then `system.passwd.conf`, `system.group.conf`
and `system.hosts.conf` convert. **This is the dangerous one**: get it wrong and
`su`, `login` and DNS stop working, so the account domains move only after a
guest gate that actually logs in and resolves a name.

Acceptance: `su` to a second account, `getent`-equivalent lookups, and name
resolution all pass in the guest **before** the files change.

### P3e — the kernel's `kernel.conf`
`kernel/kconf.c` (223 lines) gains a **freestanding** plist parser: the kernel has
no libc, and the core uses `malloc`, so it cannot be shared there. This is the one
place the "one core" decision does not reach, and pretending otherwise would be
worse than saying so. `kernel.conf` converts with its parser in the same commit,
because a boot that cannot read its own config does not boot.

### P3f — retire the legacy reader
With every file converted and every reader on plists, `libconfig.c`'s legacy
syntax goes. One commit, no behaviour change, and the diff is a deletion.

## What this plan refuses to do

* **No half-converted system at a commit boundary.** Every intermediate commit
  boots, logs in and resolves names.
* **No silent data loss.** Comments survive the C path, and wherever they cannot
  (the Cocoa skin) that is stated in the source rather than discovered later.
* **No pretending the kernel can share the core.** It cannot; P3e says so and
  writes its own parser instead of weakening the core's interface to fit.
