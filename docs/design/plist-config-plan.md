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

### P3c — the files libconfig owns (mounts, display, fonts, xfb: DONE)
**The reader audit reshaped this stage, and the guest gate is what proved it.**
The rule at the top of this plan — a file may only change format in the same
commit that its LAST reader learns the new spelling — was applied domain by
domain. The four libconfig-owned shipped domains are now plists:

| domain | readers (measured) | state |
|---|---|---|
| `system.fonts.conf` | fontconfig, through the **libconfig C API** (`fclibconf.c`) | CONVERTED |
| `system.xfb.conf` | Xfb's `hw/xfb/configargs.c` | CONVERTED |
| `system.mounts.conf` | **`init`** — now reading the domain through libconfig (record enumeration) | CONVERTED |
| `system.display.conf` | **`init`** — now through libconfig (System-over-Shared precedence is the library's) | CONVERTED |
| `fonts.conf` | libfontconfig — and it is not SHIPPED (`mk/20-userland.mk` stages no XML `fonts.conf`) | out of scope |
| `system.shells.conf` | toybox's account tools, through libconfig (no libc reader exists) | convertible, not converted |

**What the gate caught, and what fixed it.** `init` used to parse its domains
itself, and converting `system.mounts.conf` first — as this stage originally
prescribed — booted a guest with NO procfs: `init` read the plist with its line
parser, found no `=`/`}` records and mounted nothing, while libconfig was
perfectly happy. The boot still came up (devfs is mounted by the kernel, not from
the table), so only the mount-table check noticed. That conversion was reverted
and the tripwire `mounts-domain-still-legacy` was added; then the real fix landed:
**`init` reads all four of its domains through libconfig** — `mounts` as records,
`display` as keys, `network` for the hostname, `session.conf` by raw-file read —
with `-lconfig` on its link line and not one `fgets` left in the file. This is
also why the port was necessary rather than nice: toybox's `mount`/`umount` write
the mounts domain through the library, and since P3b that writer emits plists, so
the hand parser would have stranded the boot mounts on the first `mount` in a
session.

Acceptance, all measured on guests: `tests/cases/procfs_devfs.py` **15/15** — the
mount table is right (`agfs`, `proc`, `devpts`), the guest's own
`system.mounts.conf` and `system.display.conf` ARE the converted plists, and
`init-applied-the-display-domain` shows `init` read `display.width/height/bpp`
through libconfig and set 1920x1080x32 (that line prints the mode the framebuffer
reported after the ioctl, so the numbers came from the converted file).
`tests/cases/xfb_keys.py` **6/6** with `xfb-domain-is-a-plist` and
`fonts-domain-is-a-plist`. `config_plist_test` **81/81** over the ten shipped
files, and its prose check holds for a plist SOURCE too; the writer also places a
block's prose above the block's KEY, so a file's header stays at the top of the
file.

Three behaviour notes from the reader port, all deliberate: a mounts record's
EXTRA fields are ignored (the library resolves keys, not columns); a field of the
wrong TYPE reads as missing, so that record is refused with the existing
"missing fstype/target" error; and `init` gained a dynamic dependency on
`libconfig.so`, which the root image already carries for every other tool.

### P3d — musl parses plists (DONE: libc reads both spellings; the identity domains are plists)
`musl-pwconf.patch` (now 1294 lines) and `musl-hosts.patch` (319) own libc's readers
for the identity and name domains: `src/passwd/pwconf.c` parses a domain into
`struct pwentry { char *key; char *val; }` — **raw** right-hand-side text, with all
interpretation (unquoting, integers, member lists) happening at RENDER time — and
renders classic passwd/group/hosts lines for musl's own getpw*/getgr*/lookup_name to
consume. **That was the seam**, and the plist arm sits in it: a plist domain is parsed
with the shared core and flattened into the same key/raw-value list, so every
consumer below is untouched by which spelling the file uses. Detection is by CONTENT
(the first non-blank character is `<`), the rule libconfig already uses.

**The core is compiled into libc, not re-implemented there.** The toolchain rule
copies `include/plist.h` + `userland/plist.c` into `third_party/musl/src/passwd/`
before the build and removes them in the cleanup it already had, and they are now
rule prerequisites so a core change rebuilds libc. One XML implementation, three
consumers (libconfig, Foundation, libc). The cost, stated rather than discovered
later: `plist_*` also exists in libc's symbol table now, which changes nothing
observable because the same code already lives in libconfig.so and libfoundation.so.

Two mechanical facts worth keeping. `pwconf.c` is touched by BOTH patches, so it was
regenerated as a full-file addition for that ONE path (the other paths' hunks carried
over verbatim), and the hosts patch was PROVED to still apply on top — a clean-room
three-way apply, and then the real `make musl64` as the end-to-end check.

What converted: `system.passwd.conf`, `system.group.conf`, `system.hosts.conf` — in
the same commit as the reader, because libc was their LAST reader (`init` reads
mounts/display/network/session, never these; the account tools go through libconfig).
**Seven of the ten shipped domains are plists now**; the legacy three are
`system.network` (init's, librarized since P3c-b — convertible), `system.shells`
(toybox-only), and `fonts.conf` (not a libconfig domain, and not shipped).

Evidence, all measured on a guest — `tests/cases/fs_agfs.py` **9/9**:
`account-domains-read-by-libc` (`id` prints `uid=0(Admin) gid=0(Admin)`: both NAMES
resolved from the CONVERTED plists through the new arm), `owner-name-resolved`
(`ls -l` through getpwuid), and `hosts-domain-resolves` (`wget http://localhost:1/`
does not report "bad address"). The last one is the sharp one: `localhost` is
resolvable ONLY from the hosts domain — no DNS server knows it — so getpw*/lookup
succeeding IS the evidence that the domains were read. The toolchain is the other
half: `make musl64` rebuilds libc from the regenerated patches with the core inside.

THE INSTRUMENT'S OWN HISTORY, because it shaped the port: `ping` cannot be used here
— it creates an ICMP DGRAM socket BEFORE it looks a name up and this kernel refuses
that socket (`socket SOCK_DGRAM 3a: Invalid argument`; attaching a NIC did not change
it, so the refusal is the socket TYPE, not the interface). `wget` resolves first,
which is the order the check needs. Still open and NOT papered over: `su` to a second
account — the plan's full acceptance — needs a second account in the domain, and the
shipped `system.passwd.conf` has one `Admin` record.

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
