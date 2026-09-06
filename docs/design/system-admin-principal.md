# The System / Service / Admin principal model — design

Status: **DECIDED in direction** (design discussion, 2026); resolved
pieces are marked decided, review findings mark their status, and
concrete numbers (uid/gid values, helper inventory, group roster) remain
open at the bottom.

Related: `permissions-acl.md` (POSIX ACLs = the single permission
model for object access), `fsh-proposal.md` (origin ownership: `System`
= the OS, `Shared` = third parties, `Users/$USER` = the person),
`system-config-files-plan.md` (accounts in `system.passwd.conf`).

## 1. The problem with Unix

Unix conflates two different statements into one fact:

- "I am the **system administrator**" — a *person* who is allowed to
  act on the OS's behalf, and
- "I am **the system**" — the principal that *owns* the OS: uid 0,
  owner of every system file, the identity daemons run as.

In Unix both are "root". FNX's own hierarchy already disagrees with
that conflation: the FSH origin model says `/System` belongs to the OS,
`/Users/Admin` belongs to the person — yet today every file in the tree
is owned by the single human principal `Admin` at uid 0, so the origin
ownership is ceremony, not fact.

This document separates the principals and makes the filesystem state
true. The naming convention for principals is uniform: role-as-name
with an initial capital.

## 2. The principals

| principal | identity | owns | runs | login |
|---|---|---|---|---|
| **System** | uid/gid **0** | `/System`, boot state, the privileged helpers | init, boot-time services | no — the machine itself |
| **Service** | non-zero, unprivileged | what it creates under `/System/Variable Data/...` | generic daemons | no — no shell, no home |
| **Admin** | a normal **non-zero** uid/gid (open: value) | `/Users/Admin` and everything she creates | her session (shell, apps, ordinary tools) | yes — the interactive session |

`System` = the OS. `Service` = the generic unprivileged daemon runner
(the FNX answer to `daemon`/`nobody`): the default identity for
anything that must not run as System but does not earn its own account
yet. A service holding sensitive state or facing untrusted input may
still be granted a **dedicated account** (traditional per-service data
isolation); `Service` is the baseline that avoids a zoo of accounts by
default. Daemon state defaults to `/System/Variable Data/<service>`
owned by the running account; per-service isolation is the explicit
upgrade path.

`Admin` = the person. `/System` is owned by `System`, readable/
executable by all, with grants where the person may legitimately write
(see §4.1 — mediated, not raw). Files Admin creates inside a
grant-writable System directory are hers (POSIX creator-owns):
admin-authored config is Admin's; system-generated state is System's.

### 2.1 Honest scope (decided)

This model is **ownership semantics and least-privilege conventions,
not a security boundary**. euid 0 (`System`) bypasses every ACL and can
read Admin's home; there is no kernel isolation between the principals.
The value is damage-control (a compromised ordinary tool has Admin's
powers, never System's), auditable grants, and clear ownership — not
protection of System from a compromised System process or a broken
helper. File access is **one ACL model**; privileged *operations*
(mount, install, accounts, raw sockets, poweroff) are not file-shaped
and are mediated separately (§3, §4.1). The former "one permissions
model" phrasing in permissions-acl.md means object access only.

## 3. Groups (decided: role groups + privilege groups; roster open)

Groups use the same initial-capital convention and serve two purposes
under one mechanism:

| group | kind | meaning |
|---|---|---|
| `Admin` | role | **persons** who may act as the administrator |
| `Users` | role | regular user accounts |
| `Service` (group) | role | daemon-account group (primary of the Service account) |
| `Video`, `Input`, `Audio`, `Network`, … | privilege | shared device/privilege access |

- **`Admin` is the only group whose membership means "may act as the
  administrator."** It is a *person* property, not an identity
  accident — uid 0 belongs to System, so "which people may admin" has
  no uid to point at; the group carries it. Making a second person an
  administrator is `groupmod Admin + $USER` and nothing else
  (delegation for free).
- Regular accounts belong to `Users` only. Every account keeps its own
  primary gid from `system.passwd.conf`; `Admin`/`Users` are
  *supplementary* role groups.
- **Device/privilege access** (`Video`/`Input`/`Audio`) is a normal
  **group-entry ACL on the devfs node** — one ACL model, mask-capped,
  auditable like a file. `Network` is the exception: there is no
  device node for "the network", so network privilege must be enforced
  in the socket layer (supplementary-group checks in bind/raw
  socket/AF_PACKET paths) — a second enforcement point, to be built
  when the first network service needs it, not assumed to be free.
- Session services (compositor, X server, audio client) run as Admin
  in the session — not as daemons — and their device access is exactly
  what the privilege groups grant.
- `Service` carries few or no privilege groups by default.

## 4. Privilege policy: toybox never elevated

The kernel honors `S_ISUID` at exec (`kernel/syscalls/execve.c:395`).
Elevation is used **only** for a small set of native, System-owned
helpers:

```
/System/Tools/mount        mount/umount (system.mounts domain, Volumes)
/System/Tools/account      account verbs (passwd, useradd, groupadd, ...)
/System/Tools/install-app  bundle validation + install into /Applications
```

Each helper is a small dedicated binary (no shell, no generic
file-copy, strict argv/path validation, one narrow operation). The
entire toybox multicall binary stays **unprivileged**, always running
as a person: a setuid toybox is a root shell with extra steps, and no
audit closes that class (every upstream sync re-opens it). "Audit
toybox" is only meaningful for unprivileged toybox; a native userland
replacement is a separate project.

### 4.1 Helpers are how people act as administrator (decided)

- A setuid-System helper runs with euid 0, so it must authorize the
  **real** caller itself: `getuid()` and supplementary groups must
  contain `Admin`, else refuse (the classic setuid-plus-own-check
  pattern; zero kernel change).
- **The kernel stays uid-0-only.** File-level admin acts (chown of
  others' files, raw writes to System-owned files) are not granted by
  group membership in the kernel — they route through helpers. A
  delegated `Admin`-group member can run the helpers, not raw kernel
  power.
- **Machine config is edited mediated, never by raw file write.**
  There is **no person-writable ACL carve-out on
  `/System/Configuration`**: the config mechanism (running as System,
  via helper/broker) is the only writer, so Admin's "edit config" is a
  validated, canonicalizing, atomic *operation* — not a `vi` typo that
  breaks boot. The same applies to app install. (Review finding 4,
  resolved this way.)

### 4.2 Why not sudo/doas (considered, rejected)

`doas` (and sudo) is the *generic-elevation* tool: one setuid binary,
a rule file, then `exec` of whatever the rule allows. It was tested
against this model and rejected:

- **As a generic root runner it is a root shell with extra steps.**
  `doas sh`, `doas vi`, `doas toybox` — any rule that permits an
  arbitrary command yields an interactive uid-0 process, violating the
  §6 keystone directly. OpenBSD tolerates this because it accepts a
  root shell for the admin; this model does not.
- **It elevates toybox by the back door.** The account verbs
  (`passwd`, `useradd`, …) are `TOYFLAG_ROOTONLY` toybox applets
  today; a `doas passwd` rule would run toybox as root again and
  bypass §4.1's mediated config writes (raw file write instead of the
  validating account helper).
- **It conflicts with the kernel backstop (§6.4).** An allowlist that
  lets only System binaries run as root cannot also let `doas` exec
  arbitrary rule-allowed commands; the kernel cannot know doas.conf.
- **Restricted to the verb set it is merely a proto-broker.** A doas
  whose rules name only `mount`/`account`/`install-app` is a coherent
  single-suid-front-end over the helpers — but the moment per-verb
  delegation is worth having, the **System broker** (§4.3) provides the
  same rule-check with no suid binary at all. Direction: if per-verb
  delegation is ever needed, go to the broker; doas is never adopted.

### 4.3 Known cost: the helper blast radius

A compromised setuid-System helper is a compromised **machine
principal** (full ACL bypass), and helpers share one trust domain.
Mitigations: minimal helper count, no shell/file-copy primitives, and
a stated **transition trigger to the System broker** (one uid-0 daemon
owning the privileged verbs over an authenticated AF_UNIX socket, no
suid bits) — e.g. when the third helper appears, or when Admin data
becomes valuable. suid auditing is new ground for FNX; the first
helpers are an education, not a foundation. (Review findings 3 + 8.)

## 5. Boot, session, and config scope
- **init** (PID 1, uid 0) mounts from `system.mounts.conf`, runs the
  boot-time services (as System or Service per identity), then
  **drops to Admin** for the session: setuid/setgid/setgroups from the
  account domain, so no interactive process ever holds uid 0.
- **Config-scope rule (decided)**: `System` and `Service` are not
  people — no `Users/System/Configuration` or
  `Users/Service/Configuration` exists. They read the **system scope**;
  `Service` additionally reads `Shared/Configuration`. Persons get
  user→shared→system.
- **First boot re-owns the tree by origin** (chown System on /System,
  Admin on /Users/Admin); `Admin` vacates uid 0, `System` takes
  uid/gid 0 (no shell), `Service` is a non-zero account (no shell, no
  home).

## 6. Invariant: no shell as uid 0 (decided; maintenance deferred)

**It must be impossible to get a shell running as uid 0.** This is the
keystone of the model: a root shell bypasses the `Admin`-group check,
skips the helpers, and can rewrite anything, so the rest of this design
is only meaningful while it holds. Enforcement is layered:

1. **No account path (decided)**: `System` has no shell field and a
   locked credential; `su`/`login` refuse non-person targets
   (`System`, `Service`).
2. **No suid shell (decided, structural)**: the only suid binaries are
   the narrow native helpers, and helper contracts forbid spawning
   other binaries — exec can never *elevate* into a shell.
3. **init drops before exec (decided)**: init is the sole legitimate
   root process; every child drops to Admin/Service before exec, so no
   root shell is ever left behind. This is where real bugs live.
4. **Kernel backstop (open)**: an allowlist rule — a process with
   euid 0 may exec only System-owned init/helper binaries, making a
   root shell unreachable even if a helper or service is buggy and
   tries to spawn one. Small auditable exec surface; the decision to
   add it is open.

**Maintenance path (deferred)**: the broken-boot repair story (who
fixes the system when it will not boot, without a root shell) is not
yet decided — candidates are a restricted Service-toolset maintenance
mode, staged repair via boot-time System services, or an off-by-default
kernel boot-option escape hatch.

## 7. Design review (2026) — findings and resolutions

Recorded criticisms and where they landed:

1. **Not a security boundary** → decided: stated as such (§2.1).
2. **"One permissions model" overclaim** → decided: object access is
   one ACL model; operations are mediated separately (§2.1, §4).
3. **Network group is not device-node-shaped** → open until the first
   network service; socket-layer enforcement is the named path (§3).
4. **Raw ACL carve-out on /System/Configuration** → resolved against:
   mediated config edits only (§4.1).
5. **Interactions to verify before relying on grants** (open, each
   needs a rule + test when implemented):
   - ACL-writes by non-owner must clear `S_ISUID` like mode-based
     non-owner writes (kernel/syscalls.c:233) — else Admin can
     ACL-edit a System-owned suid helper and keep the bit.
   - setgid **directories**: group-shared dirs need new files to
     inherit the directory group, or group sharing mis-groups every
     new file.
   - device-ACL `mask` must be staged explicitly or the group entry is
     silently capped.
   - pty slaves must be owned by the session user at allocation.
6. **Auditability needs tooling** (open, required before carve-outs):
   `acl find --user <uid> /System` to enumerate every grant, and a
   **devfs policy table** (node class → owning group → mask) as the
   audit artifact for dynamic device nodes.
7. **Delegation** → resolved by the `Admin` role group (§3).
8. **Order of implementation** → agreed: the ownership split, Service,
   role/privilege groups, per-session membership, and
   toybox-unprivileged are the cheap 80% and come first; suid helpers,
   dedicated service accounts, and socket-layer group checks wait for
   an actual daemon/need to justify them. Build the audit tool before
   any grant sprawl.

## 8. Open items

- Admin's uid/gid numbers and Service's (`Admin` currently occupies
  uid 0 in `system.passwd.conf`; `System` takes 0 with no shell).
- The v1 helper inventory and narrow contracts (path allow-lists,
  argv validation).
- Group roster beyond `Admin`/`Users`/`Service`/`Video`/`Input`/
  `Audio`/`Network`; devfs-node group-ACL staging (which node → which
  group → which mask).
- Group-membership policy: per-session join at login vs. standing
  membership; Service's default memberships.
- Review finding 5's four interaction rules + tests.
- Review finding 6's audit tool and devfs policy table.
- `poweroff`/`reboot`: helper, kernel-keypress, or System event.
- The kernel backstop allowlist (§6.4) and the maintenance path (§6).
- `S_ISGID` at exec is unimplemented — v1 helpers are setuid-0 only.
