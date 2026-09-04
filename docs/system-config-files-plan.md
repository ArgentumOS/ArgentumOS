# System config files → libconfig domains plan

Status: DRAFT — awaiting review. Companion to `docs/config-design.md`
(the `.conf` grammar, scopes, and libconfig contract) and
`docs/fsh-proposal.md` (the FSH layout these files live in). **Supersedes
config-design §2/§5/Q-F** (scope roles + precedence — see D4), refines
§8 (namespace: `system.config` = first-party *global system settings* —
see §5.0), and amends §10 (grammar) with group records.

Scope: move the traditional Unix configuration files FNX still carries
in legacy formats — account/password data (`passwd`, `group`), resolver
identity (`hosts`, hostname, `shells`) and the boot mount table (there
is no `fstab` today; userland init hardcodes its mounts) — into
`system.config.*` domains in the `.conf` grammar. System identity and
boot-policy files live in `/System/Configuration`, which is
**authoritative**: resolution checks it first, so nothing can override
it. First-party *defaults* (Xfb etc.) live in `/Shared/Configuration`.

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
- **The current precedence is the inverse of what this plan needs.**
  config-design §5/§9 Q-F (decided, and implemented in libconfig P0):
  reads resolve **user → shared → system**, and `/Shared/Configuration`
  is "third parties, machine-wide". That is why identity files were
  shadowable and why a non-overridable tier was even needed; D4
  re-decides the order.
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
  (`hosts`, hostname, `shells`), and the boot mount table (§5.5).
- **D3 — Consumers: hard-swap.** `third_party/musl` passwd/group and
  the toybox applets are patched to read the config domains; the
  legacy-format files disappear (M7). No compatibility generator.
- **D4 — Precedence inversion (owner, latest):** reads resolve
  **system → user → shared** — `/System/Configuration` is checked
  first, so a value there **cannot be overridden at all**; the person
  (`Users/<u>/Configuration`) next, overriding *defaults* only; then
  `/Shared/Configuration`, which becomes the home of **overridable
  defaults** (first-party shipped defaults *and* third-party). This
  supersedes config-design §5/Q-F ("user → shared → system") and makes
  any separate non-overridable tier (a `Global/` dir or a trusted key —
  earlier drafts) unnecessary: system scope *is* the rule.
- **D5 — Internal defaults (owner, latest):** every first-party setting
  has an internal (compiled-in) default baked into the software. A
  user-scope `system.config.<app>` value may therefore stand alone: it
  overrides the internal default, giving purely-personal first-party
  prefs a natural home in the reserved namespace with no Shared or
  System file behind them. A `/Shared/Configuration` file ships only
  when the *shipped* baseline should itself be editable at the Shared
  tier (machine-wide defaults, §5.0).

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

## 4. The precedence model (D4)

Beneath all three file scopes sits the **internal default** (D5): if no
file scope holds a value, the software's compiled-in default applies.
A value in any scope overrides it; System > user > Shared decide which
scope value wins when several exist.

### 4.1 The order

Reads resolve **system → user → shared**:

1. `/System/Configuration/<D>.conf` — the machine's configuration.
   Checked **first**, so whatever is here wins outright: no user or
   shared file can override it, ever.
2. `Users/<u>/Configuration/<D>.conf` — the person. Consulted when
   System has no value for the key; overrides *defaults*, never System.
3. `/Shared/Configuration/<D>.conf` — **overridable defaults** (what
   FNX ships and what third parties ship). Weakest tier; used only when
   neither System nor the person set the key.
4. The app's compiled-in default.

Roles shift accordingly:

| Scope | Old role (config-design §2/§5) | New role |
|---|---|---|
| system | OS defaults, weakest | the machine's real config — authoritative, non-overridable |
| shared | third parties, machine-wide | overridable defaults (first- + third-party) |
| user | wins over everything | the person; overrides defaults, never System |

### 4.2 Why this is the non-overridable rule

No flag, no `Global/` directory, no reserved key: `/System/Configuration`
is authoritative *by construction* — it is the first place resolution
looks. Root/admin machine config goes there (root-only writes, atomic
as today); nobody can shadow it because a shadow is never consulted.

- `config -s write` = edit the machine config (root-only, unchanged).
- `config -u`/`-g` writes can never touch a System file's effect: user
  and shared files simply sit lower in the order.
- Privileged consumers (musl, M3) read the literal
  `/System/Configuration/<domain>.conf` path and need no scope logic —
  the System file is the only file that matters to them.

### 4.3 Consequences for today's content

- **Shipped first-party defaults move System → Shared**: the default
  `system.config.xfb` etc. (and the legacy identity *defaults* that a
  fresh image seeds) ship under `/Shared/Configuration` as the
  overridable baseline; a machine that wants different values writes
  them to System. (M2.)
- **Existing per-user files that shadowed system domains** stop having
  effect (e.g. a user-scope `system.config.xfb.conf` that once tuned the
  OS default now tunes nothing — the OS default itself moved to Shared,
  which the user *can* still override). Migration of stale user/shared
  shadows is Q6.
- Identity and boot-policy files (accounts, shells, mount table) are
  machine state, not defaults: they live in System only (no Shared
  baseline to ship).

## 5. The domains

Files move from legacy names/formats to `<domain>.conf` in
`/System/Configuration` (authoritative, D4). The *defaults* a fresh
image ships for overridable domains go to `/Shared/Configuration`
(M2).

### 5.0 Namespace convention

**`system.config` is reserved by convention for FNX's own (first-party)
software's settings** — both the overridable kind (which live in
`/Shared/Configuration`) and the global system settings (which live in
`/System/Configuration`), e.g. `system.config.passwd`,
`system.config.mounts`, `system.config.xfb`. It is a reserved root, not
reverse-DNS (config-design §8): third-party software never uses it and
instead keeps its own reverse-DNS domain (`com.example.<app>`).

Under D4 the convention decides scope placement by *setting kind*
(owner, latest): **overridable first-party settings go in
`system.config.<app>` domains whose files live in
`/Shared/Configuration`**, where the person may override them from the
user scope; **non-overridable first-party global system settings** have
their files in `/System/Configuration`, where system-first resolution
makes them authoritative.

- **Overridable** first-party settings (an app's shipped defaults,
  cosmetic/tunable values): `/Shared/Configuration/system.config.<app>.conf`
  — overridable from the user scope (D4 order: user > shared).
  A machine admin who wants to *lock* one writes the same domain into
  `/System/Configuration`, where it wins.
- **Non-overridable** first-party settings (identity, boot policy,
  hostname): `/System/Configuration/system.config.<domain>.conf` — no
  Shared file ships, nothing overrides it.
- **Per-user first-party preferences** are user-scope `system.config.*`
  values that override the software's **internal (compiled-in)
  default** (D5) — they may stand alone, with no Shared or System file
  behind them.

The domains in this plan (`passwd`, `group`, `shells`, `hosts`,
`mounts`) are non-overridable first-party global system settings —
their files live in `/System/Configuration` and no Shared baseline
ships. Overridable first-party examples (`system.config.xfb` and its
kind) ship in `/Shared/Configuration` instead.

### 5.1 `system.config.passwd.conf` / `system.config.group.conf`

At `/System/Configuration/` — machine state, not defaults (no Shared
baseline ships).

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

(`members` empty = no members; `password = ""` = no password.)

- **Shadow folded in** (deviation from Unix, proposed): the password
  hash lives in the record's `password` key. `/System/Configuration` is
  root-writable only, so the separate 0600 shadow file + `x`
  indirection buys nothing and costs a second file (Q2: fold — decided).
- Field naming follows `struct passwd`/`struct group` (uid, gid, gecos,
  home, shell; members) so the musl shim in M3 is a straight field map.
- **Lookup + iteration semantics** for musl: name lookup (`getpwnam`,
  `getgrnam`) and id lookup (`getpwuid`, `getgrgid`) are direct record
  reads. Sequential iteration (`getpwent`/`getgrent`) enumerates
  records and sorts by uid/gid — deterministic, and it makes the
  "first entry is root/Admin" convention unnecessary.
- `shells` is a plain list domain (§5.4), in the System scope like
  passwd; `chsh` validates against it.

### 5.2 `system.config.hosts.conf`

```
hosts = {
    localhost = {
        addresses = 127.0.0.1, ::1
        aliases =
    }
}
```

(`aliases` empty = none; §10 forbids trailing comments, so notes live
in the doc, not the examples.)

The record per hostname keeps aliases beside addresses. Consumed by
musl's name resolution (`lookup_name.c` hosts backend). The machine
name itself lives in `system.config.network.conf` (§5.3, Q4).

### 5.3 `system.config.network.conf`

At `/System/Configuration/` — the machine's network identity; a home
for `hostname` now and future network settings (Q4).

```
hostname = fnx
```

`hostname` is not a file to parse — it names the machine: at boot,
`userland/init.c` reads this key and calls `sethostname(2)`, which
feeds `uname`/`gethostname`/toybox `hostname`. A kernel-side early
name (before userland) stays a `kernel.conf`/cmdline matter, out of
scope here.

### 5.4 `system.config.shells.conf`

At `/System/Configuration/` (login policy — the valid shells accounts
bind to, same tier as passwd).

```
shells = /System/Tools/sh, /bin/sh, …
```

A plain list domain replacing the legacy `shells` line file; `chsh` +
`su` validate against it.

### 5.5 Mount table: `system.config.mounts.conf` (new "fstab")

At `/System/Configuration/` (boot policy, D4).

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
    the fixed System-scope paths (§4.2). It deliberately does not link
    `userland/libconfig.c` — libc cannot depend on the userland config
    library, and libc reads only the system files so no scope logic is
    needed. Shared source with the userland parser where practical
    (config-design §12 already proposes a shared kernel/userland lean
    parser; this is the same principle).
- **`third_party/toybox`** — `su`, `passwd`, `chsh`, `useradd`,
  `userdel`, `groupadd`, `groupdel`, `id`: read through libconfig
  (system-first rules, §4) and write through the atomic writer; `passwd`
  becomes a libconfig domain editor. `hostname` needs no patch (it
  uses the `sethostname`/`gethostname` syscalls).
- **`userland/init.c`** — mount-table consumption (§5.5).
- **`config` CLI + `userland/libconfig.c`** — group-record
  parsing/writing/enumeration (M0) + the precedence inversion (M1).
  No change
  needed in the kernel parser (§12 stays flat, §3.2).

## 7. Milestones

### M0 — Grammar: group records (libconfig + config CLI)
Extend the parser, canonical writer, and key model per §3; add
`config_record_first/next`; unit tests via the `config` CLI (write a
record domain, list, read a nested key, round-trip canonically,
duplicate-record-name parse error, `{`-prefixed bare value error).
Acceptance: nested-spelling files parse and read back identically;
flat domains unchanged; no kernel changes.

### M1 — Precedence inversion (libconfig + config CLI)
Flip resolution to system → user → shared (§4.1): a key present in
`/System/Configuration` always wins; user files override Shared
defaults only; Shared is consulted last. `config read` shows which
tier produced each value. This supersedes config-design §5/Q-F and the
libconfig P0 resolution order.
Acceptance: with the same value in all three scopes, `config read`
returns the system one; with it only in user + shared, the user one;
only in shared, the shared one; a user `-u` write no longer shadows a
System value.

### M2 — Domains ship + defaults move to Shared
`system.config.passwd.conf`, `system.config.group.conf` and the
`shells` domain land in `/System/Configuration` (identity, D4) and
replace the Makefile legacy `printf`s (Makefile userland64); the
`Admin` account re-expressed as the `admin` record. Separately, the
staging target for *overridable* first-party settings changes per
§5.0: `system.config.xfb` (today) and later first-party apps ship
their overridable settings to `/Shared/Configuration` (their domain
files' home by convention), not to `/System/Configuration` (config-design
role change).
Acceptance: `config read system.config.passwd user.admin.uid` → `0`;
images build without legacy `passwd`/`group`/`shells` files; Xfb's
default domain reads from Shared and a System copy overrides it.

### M3 — musl identity readers (hard-swap)
Patch `src/passwd/*` + the libc-internal reader; rebuild musl + the
static userland. Legacy `/etc` copies (where any remain) stop being
consulted.
Acceptance: guest tests — `getpwnam("admin")`,
`getpwuid(0)`, `getgrnam`, `getgrouplist`, `getpwent` iteration
(uid-sorted), `su`/`id`-style lookups all served from the domains.

### M4 — toybox account tools on the domains
`passwd`, `useradd`, `userdel`, `groupadd`, `groupdel`, `chsh`, `su`
read/write through libconfig; `passwd` writes hashes into the System
domain atomically.
Acceptance: guest `useradd` then `su`/login flow works against the
domain; shadow folding decided: hash lives in the user record (Q2).

### M5 — hosts / hostname
Patch musl `lookup_name.c` (hosts backend). `userland/init.c` sets the
kernel nodename from the domain's `hostname` key via `sethostname` at
boot (toybox `hostname` already works through the syscalls).
Acceptance: guest `getaddrinfo("localhost")` reflects the domain and
`hostname` prints the domain's name after boot; no hosts legacy file.

### M6 — Mount table (init)
`userland/init.c` mounts from the System `system.config.mounts.conf`.
Acceptance: boot mounts proc + devpts from the domain (edit the domain,
reboot, observe); init logs a clear error on a malformed table.

### M6b — Mount table (toybox)
toybox `mount`/`umount` consume the `system.config.mounts.conf` domain
(no-args `mount` = mount the table) in a follow-up after M6 (Q3).
Acceptance: `mount` with no args prints the domain's entries; explicit
mounts/umounts update the domain via the atomic writer.

### M7 — Decommission + sweep
Remove every legacy-format file and parser reference (Makefile, images,
docs); delete `/etc` remnants; confirm no consumer still parses a colon
file; update config-design.md (§5/§10/§12) + this doc to *implemented*.
`config` **warns on read** whenever a user/shared value has no effect
because a System value wins — no deletion (Q6).
Acceptance: full rebuild from clean + guest boot; grep sweep for
`/etc/passwd`, `/etc/group`, `/etc/hosts`, colon-parsing passwd code in
third_party patches.

## 8. Questions (owner review, resolved except Q5)

- **Q2 — Fold.** `password_hash` is a field of the user record in
  `system.config.passwd.conf` (§5.1); no shadow domain exists.
- **Q3 — M6 follow-up.** toybox `mount`/`umount` consume the mounts
  domain in M6b, after init (M6).
- **Q4 — `system.config.network.conf`.** `hostname` lives in its own
  network domain (§5.3), a home for future network settings.
- **Q5 — OPEN.** The §12 `system.config.kernel` symlink to the ESP
  `kernel.conf`: unchanged under system-first (it already resolves in
  System scope) — kept open for further discussion.
- **Q6 — Warn on read.** `config` warns when a user/shared value is
  shadowed by a System value; stale files are left in place (no
  deletion).

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
