# System config files → libconfig domains plan

Status: DRAFT — awaiting review. Companion to `docs/config-design.md`
(the `.conf` grammar, scopes, and libconfig contract) and
`docs/fsh-proposal.md` (the FSH layout these files live in). Amends
config-design §10 (grammar) and §5 (precedence) with the group-record
records extension and the `/System/Configuration/Global` tier this plan
introduces.

Scope: move the traditional Unix configuration files FNX still carries
in legacy formats — account/password data (`passwd`, `group`), resolver
identity (`hosts`, `hostname`/`shells`) and the boot mount table (there
is no `fstab` today; userland init hardcodes its mounts) — into
**non-overridable** `system.config.*` domains in the `.conf` grammar.

Decisions in this document were made by the project owner (2026):
D1–D4 below.

---

## 1. Survey findings (current state)

- **The files already live at the FSH location, in legacy formats.**
  The `userland64` Makefile target writes, straight into
  `System/Configuration/` of the staged root tree (Makefile ≈
  userland64):
  - `passwd` — `Admin:x:0:0:Admin:/Users/Admin:/System/Tools/sh`
  - `group` — `Admin:x:0:`
  - `hosts` — `127.0.0.1 localhost\n127.0.0.1 (none)`
  - `shells` — `/System/Tools/sh`
  - alongside the one real domain file:
    `system.config.xfb.conf` (shipped from `userland/configuration/`,
    c11c02b).
- **The consumers parse those legacy formats themselves, hard-coded:
  - `third_party/musl` is the libc: `src/passwd/*` (`getpw_a.c`,
    `getgr_a.c`, `getpwent*.c`, `getgrent*.c`, `getgrouplist.c`,
    `fgetpwent.c` …) parse the colon files; `src/network/lookup_name.c`
    reads `/etc/hosts` via `__fopen_rb_ca`.
  - `third_party/toybox` applets read them directly: `su`, `passwd`,
    `chsh` (`toys/lsb/`), `id` (`toys/posix/`),
    `groupadd/groupdel/useradd/userdel` (`toys/pending/`).
  - **Hostname has no file consumer**: musl `gethostname` is
    `uname()`-based and toybox `hostname` only calls
    `gethostname`/`sethostname`. The machine name is the *kernel
    nodename* — configured at boot, not read from a file. The legacy
    "hostname file" therefore reduces to "who calls `sethostname` at
    boot".
- **The config machinery to build on** (config-design.md, done): flat
  dot-nested `key = value` grammar (§10, normative), three scopes with
  roots `/System/Configuration`, `/Shared/Configuration`,
  `/Users/<u>/Configuration` (§2), resolution user → shared → system
  per key (§5), `include/libconfig.h` + `userland/libconfig.c`
  (typed getters, scope-explicit writes, atomic write temp+fsync+
  rename, §11), the `config` CLI (`userland/config.c`), and the
  kernel-domain precedent (§12: `/System/ESP/kernel.conf` read through
  a `/System/Configuration/system.config.kernel.conf` **symlink**,
  canonical writer resolving the final path component). All config
  values are scalars or arrays — **there is no way to express a list of
  records** (passwd users, mounts) today.
- **No `fstab` exists.** `userland/init.c` (PID 1) hardcodes
  `try_mount("proc", "/System/Processes")` and
  `try_mount("devpts", "/System/Devices/pts")`; toybox `mount`/`umount`
  have no mount table to read. A "fstab" here is therefore a new
  mount-table domain, not a migration.

## 2. Decisions (project owner, locked)

- **D1 — Record model: extend the `.conf` grammar with group
  records** (§3 below). Real nested block values, normative in §10.
- **D2 — Scope of this plan: full sweep** — account identity (`passwd`,
  `group`, with shadow folded in, see §5.1), resolver identity
  (`hosts`, hostname), and the boot mount table (§5.4).
- **D3 — Consumers: hard-swap.** `third_party/musl` passwd/group and
  the toybox applets are patched to read the config domains; the
  legacy-format files disappear (M7). No compatibility generator.
- **D4 — Non-overridable = location.** Configs stored under
  `/System/Configuration/Global/` cannot be overridden at all — no
  shared- or user-scope file can shadow them and no non-root write can
  change them (a "better idea" replacing an earlier per-file trusted
  key; see §4). Ordinary domains (e.g. the current Xfb domain) keep
  today's user-overridable resolution by simply living outside
  Global/.

## 3. Grammar amendment (§10): group records

### 3.1 Syntax (normative addition)

```
assignment := WS* key WS* '=' WS* value WS*         (existing)
            | WS* key WS* '{' group-line* '}' WS*   (NEW: block value)

group-line := empty | comment | assignment           (nested, recursive)
```

- A block value opens when the first non-WS char after `=` is `{`.
  A bare value beginning with `{` therefore becomes a parse error —
  quote it (`key = "{notablock"`). This is the only ambiguity change;
  values are otherwise untouched.
- Blocks nest arbitrarily. Inside a block, keys are relative to the
  outer key (`.` is implied): the assignment `uid = 0` inside
  `user = { root = { … } }` is the key `user.root.uid`.
- A **record** is a block whose body contains further named blocks:
  each inner block with a distinct name is one record. Duplicate record
  names inside one block are a parse error (unlike duplicate flat keys,
  which stay last-wins — records are identity-bearing and must not
  silently collapse).
- Empty block `key = {}` is valid.
- The **canonical writer** (§10 "Writing") emits record domains in the
  nested spelling and flat domains exactly as today. Flat and nested
  spellings of the same keys may be mixed in one file; reads never care
  which spelling produced the key.

### 3.2 Rationale

The in-memory model in libconfig stays a flat key map (keys contain
dots) — blocks are a *file spelling* for grouped, ordered, named
records; no tree data model is introduced. The §12 kernel parser is a
flat subset and is **unchanged**: `kernel.conf` is a flat domain and the
canonical writer only emits nested spelling for record domains, which
userland owns. ("One grammar" is preserved: the same file, same keys,
same reader rules — only the writer chooses the nested form where
records need it.)

### 3.3 Record enumeration API

Record domains need ordered enumeration and per-record access. Add to
`include/libconfig.h` (userland):

```
config_record_first(domain, "user")          → first record name
config_record_next(domain, "user", prev)     → next, in source order
config_get(domain, "user.<name>.uid")        → existing typed getter
```

Source order is what the canonical writer preserves (it never reorders
records). Consumers that need Unix orderings (getpwent by uid) sort
themselves; see §5.1.

## 4. Non-overridable configuration: `/System/Configuration/Global` (D4)

Non-overridable domains live in a dedicated subdirectory of the system
scope: `/System/Configuration/Global/`. A config stored there **cannot
be overridden at all** — no shared-scope file and no user-scope file can
shadow it, and no non-root write can change it. Overridability is
decided by *where the system file lives*, not by a marker inside it.

### 4.1 The rule

For a domain D, look up its system-scope file:

- `/System/Configuration/Global/<D>.conf` → **global**: authoritative.
  libconfig resolution (config-design §5) never consults Shared or user
  scope for D; any stale shared/user files for D are ignored on reads.
- `/System/Configuration/<D>.conf` → **ordinary**: today's user →
  shared → system per-key resolution, unchanged.

A domain is global iff its system file is in `Global/`. Root promotes
or demotes a domain by moving the file — no content change, no flags.

### 4.2 Semantics honored by libconfig and the `config` CLI

For a global domain (system file under `Global/`):

- **Reads are locked to the Global file** — resolution short-circuits
  after system; shared and user scope are never consulted.
- **`config write`/`config delete` to the shared or user scope for the
  domain are refused** with `ACCESS` (you may not shadow a Global
  domain from an overridable scope).
- **Writes to the Global file are root-only** (`/System/Configuration`
  is root-writable per the FSH; `Global/` is the same) and go through
  the normal atomic writer (temp + rename, inside `Global/`).
- Privileged consumers (musl, M3) read the literal
  `/System/Configuration/Global/<domain>.conf` path, so the rule cannot
  be bypassed through a non-system configuration tree, a symlink, or a
  stale scope file.

### 4.3 Why a directory, not a flag

- **No new file syntax**: nothing to parse, and nothing a non-root
  writer could forge *inside* a file — the location is what
  root controls.
- **Scales to any domain**: Xfb's `system.config.xfb` stays overridable
  exactly where it is; the identity and mount domains move into
  `Global/` when this plan lands.
- **Visible in the filesystem**: `config list` and `ls` show the tier
  directly; the FSH promise ("all machine settings in one place", no
  `/etc` split) is kept, with a documented second tier inside
  `/System/Configuration`.
- `kernel.conf` (§12, read through its symlink) is boot identity: it
  belongs in `Global/` (Q5 covers symlinks under Global).

## 5. The domains

Files move from legacy names/formats to `<domain>.conf`. Identity and
boot-policy domains live under `/System/Configuration/Global/` (D4);
ordinary domains stay at `/System/Configuration/`. Names below use the
first-party `system.config.*` prefix per config-design §2/§8.

### 5.1 `system.config.passwd.conf` / `system.config.group.conf`

Live at `/System/Configuration/Global/` (identity, D4).

```
user = {
    admin = {
        uid = 0
        gid = 0
        gecos = "Admin"
        home = "/Users/Admin"
        shell = "/System/Tools/sh"
        password = ""
    }
}

group = {
    admin = {
        gid = 0
        members =
    }
}
```

(`members` empty = no members; `password = ""` = no password — see Q2.)

- **Shadow folded in** (deviation from Unix, proposed): the password
  hash lives in the record's `password` key. `Global/` is root-only, so
  the separate 0600 shadow file + `x` indirection buys nothing and
  costs a second file. Marked Q2.
- Field naming follows `struct passwd`/`struct group` (uid, gid, gecos,
  home, shell; members) so the musl shim in M3 is a straight field map.
- **Lookup + iteration semantics** for musl: name lookup (`getpwnam`,
  `getgrnam`) and id lookup (`getpwuid`, `getgrgid`) are direct record
  reads. Sequential iteration (`getpwent`/`getgrent`) enumerates
  records and sorts by uid/gid — deterministic, and it makes the
  "first entry is root/Admin" convention unnecessary.
- `shells` is a plain list domain (§5.3), also under `Global/`;
  `chsh` validates against it.

### 5.2 `system.config.hosts.conf` / hostname

```
hosts = {
    localhost = {
        addresses = 127.0.0.1, ::1
        aliases =
    }
}
hostname = fnx
```

(`aliases` empty = none; §10 forbids trailing comments, so notes live
in the doc, not the examples.)

The record per hostname keeps aliases beside addresses. `hosts` is
consumed by musl's name resolution (`lookup_name.c` hosts backend).
`hostname` is not a file to parse — it names the machine: at boot,
`userland/init.c` reads this key and calls `sethostname(2)`, which
feeds `uname`/`gethostname`/toybox `hostname`. A kernel-side early
name (before userland) stays a `kernel.conf`/cmdline matter, out of
scope here. (Q4: whether `hostname` stays a key in this domain or its
own.)

### 5.3 `system.config.shells.conf`

Lives at `/System/Configuration/Global/` (login policy — the valid
shells bound accounts to, same tier as passwd).

```
shells = /System/Tools/sh, /bin/sh, …
```

A plain list domain replacing the legacy `shells` line file; `chsh` +
`su` validate against it.

### 5.4 Mount table: `system.config.mounts.conf` (new "fstab")

Lives at `/System/Configuration/Global/` (boot policy, D4).

```
mount = {
    processes = {
        fstype = proc
        target = "/System/Processes"
    }
    pts = {
        fstype = devpts
        target = "/System/Devices/pts"
    }
}
```

- `userland/init.c` replaces its hardcoded `try_mount()` calls with an
  ordered mount of this domain (record order = mount order), so the
  boot mount set becomes machine configuration.
- Q3: whether toybox `mount`/`umount` (no-args = mount the table /
  unmount table entries) should consume it in this plan or a follow-up.

## 6. Consumers to patch (D3)

- **`third_party/musl`** — rebuilt libc (ripples to every static
  userland binary; the standard userland rebuild):
  - `src/passwd/*`: `getpw_a.c`/`getpw_r.c`/`getpwent*.c` (passwd),
    `getgr_a.c`/`getgr_r.c`/`getgrent*.c` (group), `getgrouplist.c`,
    `fgetpwent.c`/`fgetgrent.c` (keep, but reading the domain needs no
    stream equivalent — see M3 acceptance).
  - `src/network/lookup_name.c` hosts branch.
  - musl needs a small **libc-internal reader**: a read-only subset of
    the grammar (flat + block values, strings/ints/bools/arrays) plus
    the fixed Global paths (§4.2). It deliberately does not link
    `userland/libconfig.c` — libc cannot depend on the userland config
    library, and libc reads only the system files so no scope logic is
    needed. Shared source with the userland parser where practical
    (config-design §12 already proposes a shared kernel/userland lean
    parser; this is the same principle).
- **`third_party/toybox`** — `su`, `passwd`, `chsh`, `useradd`,
  `userdel`, `groupadd`, `groupdel`, `id`: read through libconfig
  (Global rules, §4) and write through the atomic writer; `passwd`
  becomes a libconfig domain editor. `hostname` needs no patch (it
  uses the `sethostname`/`gethostname` syscalls).
- **`userland/init.c`** — mount-table consumption (§5.4).
- **`config` CLI + `userland/libconfig.c`** — group-record
  parsing/writing/enumeration (M0) + the Global tier (M1). No change
  needed in the kernel parser (§12 stays flat, §3.2).

## 7. Milestones

### M0 — Grammar: group records (libconfig + config CLI)
Extend the parser, canonical writer, and key model per §3; add
`config_record_first/next`; unit tests via the `config` CLI (write a
record domain, list, read a nested key, round-trip canonically,
duplicate-record-name parse error, `{`-prefixed bare value error).
Acceptance: nested-spelling files parse and read back identically;
flat domains unchanged; no kernel changes.

### M1 — Global tier (libconfig + config CLI)
Resolution consults `Global/` first: a domain whose system file is
under `/System/Configuration/Global/` is locked to that file (no
shared/user fallback) and `config write`/`delete` to -u/-g scopes for
it are refused with `ACCESS` (§4); `config read` reports the tier.
Acceptance: a domain moved into `Global/` cannot be shadowed from
Shared or user scope; a domain outside it keeps the §5 resolution.

### M2 — Account domains ship + migrate
`system.config.passwd.conf` + `system.config.group.conf` + the
`shells` domain ship under `/System/Configuration/Global/` (identity)
and replace the Makefile legacy `printf`s (Makefile
userland64); `Admin` account re-expressed as the `admin` record; a
first-boot/root scaffold validates them with `config`.
Acceptance: `config read system.config.passwd user.admin.uid` → `0`;
images build without legacy `passwd`/`group`/`shells` files.

### M3 — musl identity readers (hard-swap)
Patch `src/passwd/*` + the libc-internal reader; rebuild musl + the
static userland. Legacy `/etc` copies (where any remain) stop being
consulted.
Acceptance: guest tests — `getpwnam("admin")`,
`getpwuid(0)`, `getgrnam`, `getgrouplist`, `getpwent` iteration
(uid-sorted), `su`/`id`-style lookups all served from the domains.

### M4 — toybox account tools on the domains
`passwd`, `useradd`, `userdel`, `groupadd`, `groupdel`, `chsh`, `su`
read/write through libconfig; `passwd` writes hashes into the Global
domain atomically.
Acceptance: guest `useradd` then `su`/login flow works against the
domain; shadow-folding decision (Q2) resolved.

### M5 — hosts / hostname
Patch musl `lookup_name.c` (hosts backend). `userland/init.c` sets the
kernel nodename from the domain's `hostname` key via `sethostname` at
boot (toybox `hostname` already works through the syscalls).
Acceptance: guest `getaddrinfo("localhost")` reflects the domain and
`hostname` prints the domain's name after boot; no hosts legacy file.

### M6 — Mount table
`userland/init.c` mounts from the Global `system.config.mounts.conf`; Q3 decision
on toybox `mount`/`umount`.
Acceptance: boot mounts proc + devpts from the domain (edit the domain,
reboot, observe); init logs a clear error on a malformed table.

### M7 — Decommission + sweep
Remove every legacy-format file and parser reference (Makefile, images,
docs); delete `/etc` remnants; confirm no consumer still parses a colon
file; update config-design.md (§5/§10/§12) + this doc to *implemented*.
Acceptance: full rebuild from clean + guest boot; grep sweep for
`/etc/passwd`, `/etc/group`, `/etc/hosts`, colon-parsing passwd code in
third_party patches.

## 8. Open items (Q)

- **Q1** — Are `Global/` files also non-writable to root-through-
  libconfig users (a `config -s` write policy beyond "root can"), or is
  the file permission the only write gate?
- **Q2** — Shadow folding (hash in the record, §5.1) vs. a separate
  Global `system.config.shadow.conf` with `x` indirection.
- **Q3** — toybox `mount`/`umount` consuming the mount table in M6 or
  a follow-up.
- **Q4** — `hostname` as a key inside `system.config.hosts` vs. its own
  `system.config.network`/`hostname` domain.
- **Q6** — Should `Shared/Configuration` gain its own non-overridable
  `Global/` for third-party machine-wide settings, or is
  `/System/Configuration/Global` the only tier for now?
- **Q5** — Symlinks under `Global/` (the §12 `system.config.kernel`
  symlink to the ESP `kernel.conf`): allowed as global domains
  (the rule follows the resolved target's location), or rejected?

## 9. References

- `docs/config-design.md` — grammar (§10), scopes/precedence (§5),
  libconfig (§11), kernel.conf + symlink domain precedent (§12).
- `docs/fsh-proposal.md` — `/System/Configuration` etc. + the
  `Configuration/` policy in §3.
- `Makefile` userland64 — today's legacy file generation.
- `third_party/musl/src/passwd/*`, `src/network/lookup_name.c`,
  `third_party/toybox/toys/{lsb,pending}/…` — consumers.
- `userland/libconfig.c`, `userland/config.c`, `userland/init.c`,
  `include/libconfig.h` — config machinery + PID-1 mounts.
