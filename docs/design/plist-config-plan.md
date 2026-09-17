# Converting FNX config to XML plists — plan

Status: **P3a and P3b DONE** (2026-09-17: the core keeps comments, and libconfig
reads plists — its writer now EMITS them). **Next: P3c**, converting the files
libconfig owns, `system.mounts.conf` first because `init` reads it at boot.
Decisions taken 2026-09-17 by the user: format is **XML plist v1.0**; sharing is
**one C core, two skins** (see `docs/design/foundation-plan.md`); scope is
**everything, `kernel.conf` included**; and **comments are preserved** — the core
keeps them.

## What exists today, measured

| thing | size | note |
|---|---|---|
| `userland/libconfig.c` + `libconfig_plist.c` | 4104 + 930 lines | the config library: the legacy grammar and the plist spelling over ONE entry model (`libconfig_internal.h`), P3b |
| `userland/tools/config.c` | 937 lines | the `config` CLI |
| `include/plist.h` + `userland/plist.c` | 160 + 1261 lines | the plist core and its skin (P1/P2/P3a, landed) |
| `userland/tests/plist_test.c` + `config_plist_test.c` | 337 + 495 lines | the core's acceptance (39 checks) and P3b's both-ways acceptance |
| the shipped files | **10 `.conf`** | under `userland/configuration/` |
| `kernel/kconf.c` | 223 lines | the KERNEL's own parser for `kernel.conf` |
| `third_party/musl-pwconf.patch` | **987 lines** | libc's own reader for passwd/group |
| `third_party/musl-hosts.patch` | 319 lines | libc's own reader for hosts |

So the same files are parsed by **five** readers: libconfig, the `config` tool,
`init` (mounts), Xfb's `configargs`, **libc**, and the **kernel**. That is the
whole difficulty of this conversion, and it is why the order below is what it is.

**The files are mostly documentation.** `system.display.conf` is 53 comment lines
of 60; `system.fonts.conf` 25 of 28. The plist writer used to emit no comments at
all, which is why the core change below came first: without it, the first
`config set` would have silently deleted the prose — including the header of
`system.mounts.conf` that explains what `fstype` and `source` mean.


## The rule that governs the order

**A file may only change format in the same commit that its last reader learns
the new one.** Converting a file first means the login system, name resolution or
the mount table stops working — the class of breakage that locks you out of your
own machine. Every stage below therefore converts a reader *before* the files it
reads, and the reader accepts **both** formats during the transition.

## Stages

### P3a — the core keeps comments (DONE, 2026-09-17)
`plist_value_t` gained `PLIST_COMMENT`, holding the text between `<!--` and
`-->` verbatim. The parser **retains** comments as items instead of skipping
them, and the serialiser writes them back **in place** at the same indent.
`plist_skip_space` is whitespace-only now; `plist_capture_comments` takes the
comments (and the whitespace around them) at every position an item may follow —
including between a `<key>` and its value — and **discards** them at a position
that has nowhere to keep one.

Containment, because the tree's shape has to tolerate them:
* in an **array**, a comment is an item of type `PLIST_COMMENT`;
* in a **dictionary**, it occupies a slot whose key is **NULL** — the one
  sentinel in the pair arrays, so `plist_dictionary_get` and the count/iteration
  accessors skip it (they would fault on `strcmp(NULL, …)` otherwise);
* the Objective-C skin **drops** comments, which is correct rather than a gap:
  Cocoa has no comment type, so `NSPropertyListSerialization` must not invent
  one. The asymmetry is deliberate — the C path (libconfig, the `config` tool)
  preserves comments, the Cocoa path does not, and each is documented where it
  matters.

**One simplification, chosen rather than fixed:** a comment between `<plist …>`
and the root value is ACCEPTED AND DISCARDED, not hoisted into the tree. A
file's header prose belongs *inside* the root dictionary, where it round-trips
natively, and hoisting here would mean re-shaping whichever type the root
happens to be — a `<string>` root has no slot to hoist a comment into. It
removed ~40 lines of array shifting, and it is what the converted files will do.

Also landed with it: `plist_new_comment`; a private `plist_append_comment` (an
item in an array, a NULL-key slot in a dictionary); and `plist_dictionary_set`
split into a NULL-tolerant core with the public setter keeping its refusal of a
NULL key.

Acceptance MET, on the host and cross-compiled for the guest
(`tools/musl-clang64.sh`, the skin with `tools/musl-clang-objc64.sh`): a
commented document round-trips **byte-identically**
(`comments-roundtrip-byte-identical`), comments stay attached to the entry they
precede, a `config set`-style rewrite leaves the prose alone, an unterminated
comment is REFUSED, and the 28 pre-existing checks still pass unchanged — the
core probe is **39/39**.

### P3b — libconfig reads plists, and writes them (DONE, 2026-09-17)
`libconfig.c` is now **two translation units over the core**: `libconfig.c` (the
legacy grammar, the resolver and the public API — unchanged) and a new
`libconfig_plist.c` (the plist spelling), sharing the entry model in
`libconfig_internal.h`. Both spellings parse into the same flat, ordered entry
list, so nothing downstream of the parse knows which one the file used: a nested
`<dict>` is the plist spelling of `key = { … }` and flattens to `key.child`, and
a `<dict>` nested deeper is a `CONFIG_TYPE_RECORD`, exactly as a `{ … }` value
inside an array is. A file's spelling is decided by CONTENT (its first non-blank
character is `<`), never by name.

**The writer emits plists, unconditionally** — the user's decision
(2026-09-17), taken with its consequence in view: a `config set` on a domain
whose other readers lag (`system.passwd.conf`, `system.group.conf`,
`system.hosts.conf` through musl's patches, and `kernel.conf` through
`kernel/kconf.c`) CONVERTS that file to a spelling those readers cannot yet
read, so **P3d/P3e are now on the critical path for writes as well as reads**.

Prose survives the conversion: a legacy `#` line and a plist `<!-- … -->` both
land on the entry they precede, and the writer emits them as XML comments — the
whole reason P3a exists. A comment that preceded nothing is the file's closing
prose, read back out of the file being replaced (`config_trailer_prose`), so the
read paths never have to carry it. A file whose only content is prose is no
longer deleted by an `unset` of its last key.

Three consequences recorded rather than discovered later: a comment INSIDE an
array is dropped (the config model has no comment slot in a value); a
comment that opened a block moves one line in, to the entry it precedes; and a
top-level group whose keys interleave with another group's is regrouped by the
writer, which is what the legacy writer already did.

Also added: `plist_comment_append` in the core — a WRITER must be able to build
a commented tree, not only the parser, or the first `config set` deletes the
prose P3a was built to keep.

Acceptance MET by `userland/tests/config_plist_test.c`, which takes every shipped
`.conf`, copies it into a scratch tree, forces a rewrite (the rewrite IS the
conversion), and compares every key in order, every type and value, the prose
line count and the first comment's text — then asserts the plist spelling is
STABLE across a second rewrite, which holds only if the plist reader produced the
same entries the writer wrote. **9 of the 10 shipped files convert with every
check green**; the tenth is skipped as not a libconfig domain, and says so (see
P3c). The probe is staged for the guest.

### P3c — convert the files libconfig owns (`system.fonts` + `system.xfb` DONE; mounts/display BLOCKED on init)
**The reader audit reshaped this stage, and the guest gate is what proved it.**
The rule at the top of this plan — a file may only change format in the same
commit that its LAST reader learns the new spelling — was applied domain by
domain, and two of them have a reader that is NOT libconfig:

| domain | readers (measured) | state |
|---|---|---|
| `system.fonts.conf` | fontconfig, through the **libconfig C API** (`third_party/x11/fontconfig-libconf.patch`: `fclibconf.c`) | **CONVERTED** |
| `system.xfb.conf` | Xfb's `hw/xfb/configargs.c`, through libconfig | **CONVERTED** |
| `system.mounts.conf` | `init`'s OWN line parser (`tools/init.c`: `mount_from_table`), beside libconfig | BLOCKED |
| `system.display.conf` | `init`'s own parser (`set_display_mode_from_domain`, which re-implements libconfig's System-over-Shared precedence) | BLOCKED |
| `fonts.conf` | libfontconfig — and it is not SHIPPED (`mk/20-userland.mk` stages no XML `fonts.conf`) | out of scope |
| `system.shells.conf` | toybox's account tools, through libconfig (no libc reader exists) | convertible |

**What the gate caught.** Converting `system.mounts.conf` first — as this stage
originally prescribed — booted a guest with no procfs: `init` read the plist with
its line parser, found no `=`/`}` records and mounted nothing, while libconfig was
perfectly happy. The boot still came up (devfs is mounted by the kernel, not from
the table), which is exactly why the mount-table check exists. The conversion was
**reverted**, and `tests/cases/procfs_devfs.py` now carries the tripwire
`mounts-domain-still-legacy`: it fails if that file converts before `init` can
read it.

**So what remains here is a CODE change, not a file change:** `init` must read its
four domains (`mounts`, `display`, `network`, `session`) through libconfig instead
of parsing them itself — the plan's own "convert the reader FIRST" rule applied to
the last non-libconfig reader. `system.mounts.conf` and `system.display.conf`
convert in that same commit.

**`fonts.conf` is NOT one of them** (measured while writing P3b's probe): it is
fontconfig's own XML — `<fontconfig>`, `<dir>`, `<match>` — read by libfontconfig,
with no `<plist>` in it and no `key = value` line either. It converts only if
fontconfig's reader moves onto the core, which is a separate decision from this
plan's. The probe reports it as skipped rather than failing it.

Acceptance for what DID convert, all measured on a guest:
`tests/cases/xfb_keys.py` asserts `xfb-domain-is-a-plist` and
`fonts-domain-is-a-plist` — the guest's own copies ARE the converted files — with
the X server up and answering typed keys from the `system.xfb.conf` it read.
`config_plist_test` shows both files keep every key in order, every type and
value, and their prose, and are byte-stable across a rewrite; the writer also
places a block's prose above the block's KEY, so a file's header stays at the top
of the file instead of inside its first record.

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

**This is also where the legacy WRITER goes.** P3b left `libconfig.c`'s emitter
chain (`emit_leaf`, `emit_value_lines`, `write_indented`, `value_to_text`,
`emit_children`, `emit_grouped`) in place but uncalled — `emit_grouped` is the
root of that dead subgraph, and the section is labelled as dead in the source.
They are the writer half of the grammar P3f deletes, so they go in the same
deletion rather than swelling P3b's diff.

## What this plan refuses to do

* **No half-converted system at a commit boundary.** Every intermediate commit
  boots, logs in and resolves names.
* **No silent data loss.** Comments survive the C path, and wherever they cannot
  (the Cocoa skin) that is stated in the source rather than discovered later.
* **No pretending the kernel can share the core.** It cannot; P3e says so and
  writes its own parser instead of weakening the core's interface to fit.
