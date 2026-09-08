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
| **System** | uid/gid **0** | `/System`, boot state, the privileged helpers | init, sessionmgr, boot-time services | no — the machine itself |
| **Display** | non-zero, unprivileged | the display: Xfb, the pre-login greeters (console + graphical), the system menubar | the always-on X server and the login screen | no — no shell, no home |
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

`Display` = the login screen's unprivileged owner (decided 2026-09):
Xfb and the pre-login greeters never run as System — the always-on X
server, the console and graphical greeters, and the pre-login system
menubar run as `Display`, an unprivileged account whose device access
comes from the `Video`/`Input` privilege-group ACLs on the devfs nodes
it opens (fb0, psaux, ...). It owns no home and no shell; its state
(display logs, cookies) lives under `/System/Variable Data/Display`.
`sessionmgr` (System) is the only privileged piece at the login
screen: it spawns the greeters as `Display` and performs the
verify-and-drop into a person's session.

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
/System/Tools/disk         "disk mount <device|guid> <target>" / "disk unmount <target|device|guid>" /
                           "disk initialize mbr|gpt <device>" (destructive whole-disk verbs) /
                           "disk partition create <device> <type> <size>" /
                           "disk partition delete <device> <partition-guid>" /
                           "disk eject <device|target|guid>" (unmounts, then ejects removable media) /
                           "disk info <device|target|guid>" (read-only; non-Admin) /
                           "disk list" (all disks + partitions; read-only; non-Admin)
                           (type = agfs|swap|esp by reserved GUID; table auto-detected GPT|MBR)
/System/Tools/account      person-object verbs: "account <user> add|delete|password|group <g> add|remove|shell <sh>"
/System/Tools/group        group-object verbs: "group <name> create|delete" (Admin)
/System/Tools/install     "install <bundle> <global|local>" — validate + install a bundle
                           (app bundles → /Applications | ~/Applications; shared-resource bundles
                           (libraries/fonts/resources only) → /Shared | ~/Shared by payload type;
                           global = Admin, local = self no-elevation; the file manager's
                           drag-and-drop installs via this helper, both kinds)
/System/Tools/uninstall   symlink to install (argv[0]-dispatched): "uninstall <bundle-name> <global|local>"
/System/Tools/power        off / reboot (the everyday verb: "power off")
/System/Tools/config       "config <domain> ..." — the mediated .conf writer: with Admin, any scope incl.
                           any system domain (allowlist lives in the tool: Admin gate only, no domain
                           restriction); without Admin, the caller's own user scope only
```

Each helper is a small dedicated binary (no shell, no generic
file-copy, no spawning, strict argv/path validation, one narrow
operation). The entire toybox multicall binary stays **unprivileged**, always running
as a person: a setuid toybox is a root shell with extra steps, and no
audit closes that class (every upstream sync re-opens it). "Audit
toybox" is only meaningful for unprivileged toybox; a native userland
replacement is a separate project.

### 4.4 Object-shaped vs operation-shaped privilege (result, 2026-09)

Privilege splits into two classes, and the split decides whether
elevation is needed at all:

- **Operation-shaped** privileges are not objects — there is nothing
to hang an ACL on. Rebooting is not a file; passwd-conf integrity is
not a file; mount policy is not a file. These are the **helpers**: a
closed, first-party, System-owned fleet, each a narrow setuid
binary.
- **Object-shaped** privileges are file-like and belong on the
object's ACL, never in a binary. Device access is the existing proof:
`Video`/`Input`/`Audio` are group-entry ACLs on the devfs node, read
off the node like any file. **Raw block devices are the same shape**:
`@Disk/...` nodes carry a role-group ACL, so formatting/partitioning
tools (`disk`, mkfs-style) run **unprivileged**, writing through the
node's ACL like an ordinary file — "can I format this disk?" reads
`acl get @Disk/...`. The rev-1 intuition that disk work needs a suid
`disk` helper was wrong: the raw-block path is already file-shaped;
it was only ever exercised as root because every node was owned by
uid-0 Admin. **Carve-out (2026-09): partition-table *creation* is a
privileged `disk` verb** (`disk initialize mbr|gpt <device>`) — it
rewrites a whole disk and can destroy the boot/root device, so it is
Admin-mediated; formatting *within an existing partition* remains
unprivileged node-ACL work.

Ground truth for "needs privilege" (2026-09): the kernel gates these
syscalls on `IS_SUPERUSER` (`current->euid == 0`,
`include/fnx/process.h:54`): mount, umount, reboot, settimeofday,
sethostname, chroot, mknod, ioperm/iopl, chown-of-others, priority/
prlimit on others, parts of ipc/msgctl, setfsuid. **Not gated at
all**: raw/AF_PACKET sockets (`net/packet.c`) — open to any caller
today; the §3 `Network` socket-layer group check is the named fix,
still deferred to the first network service. Today's de-facto suid is
the whole toybox binary staged 4755 so `TOYFLAG_ROOTONLY` applets
(passwd, useradd, ...) work — the thing this model retires.

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
  `/System/Configuration`**: the `config` helper (setuid-System) is the
  mediated writer — with Admin privileges it may adjust **any** config
  domain at any scope (the allowlist lives in the tool: the Admin gate
  is the whole restriction, no per-domain list), and without Admin it
  writes only the caller's own user scope. `account`/`group`/`install`
  are *validated front-ends* that write their own domains through the
  same atomic mechanism — not exclusive writers; a raw Admin `config
  -s system.passwd` edit is permitted the way a root `vipw` is. So
  Admin's "edit config" is a canonicalizing, atomic *operation* — not
  a `vi` typo that breaks boot. (Schema-less by design: validation is
  grammar + atomicity, not meaning — a semantically wrong value is the
  operator's error, accepted.) The same applies to app install. (Review finding 4,
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
- **Restricted to the verb set it is merely a proto-helper-fleet.** A
  doas whose rules name only `mount`/`account`/`install` is a
  coherent single-suid-front-end over the helpers — i.e. it reinvents
  the fleet with an extra indirection and a rule file to audit. The
  helpers themselves already are that front end, one narrow binary per
  verb. Direction: doas is never adopted; per-verb delegation, if ever
  needed, is a property of the helper set (or the account/config
  mechanism), not a generic root runner (finding 9).

### 4.3 Known cost: the helper blast radius

A compromised setuid-System helper is a compromised **machine
principal** (full ACL bypass), and helpers share one trust domain.
Mitigations: minimal helper count, no shell/file-copy primitives, no
spawning, System-owned + immutable after the ownership split (a helper
a person can replace is meaningless), and the §6.4 exec allowlist as
the backstop that keeps even a *buggy* helper from yielding a shell.
The helpers are the durable shape for operation-shaped privilege
(§4.4), not a stopgap: the considered alternative — one privileged
daemon ("System broker") — was rejected as systemd-shaped (finding 9),
and per-verb delegation, if ever needed, belongs in the helper set or
the doas-rejection's §4.2 reasoning, never a generic root runner.
(Review findings 3 + 8 + 9–12.)

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

## 6. Invariant: no path to an interactive root shell (decided; maintenance deferred)

**It must be impossible to get a shell running as uid 0.** This is the
keystone of the model: a root shell bypasses the `Admin`-group check,
skips the helpers, and can rewrite anything, so the rest of this design
is only meaningful while it holds. (Setuid 0 itself is not the
invariant's subject — the helpers hold euid 0 by design; see finding
11.) Enforcement is layered:

1. **No account path (decided)**: `System` has no shell field and a
   locked credential; **`su` does not exist** (decided: person-switch is
   logout/login; su's two Unix jobs — the root shell and acting as
   another person — are respectively forbidden by §6 and owned by the
   session model) and `login` refuses non-person targets
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
9. **A privileged daemon ("System broker")** → rejected — too
   systemd-shaped: one uid-0 process owning verbs, service spawning,
   and config is the exact architecture this model rejects. No
   privileged userland daemon exists beyond init; the helpers are the
   shape.
10. **Privilege as a kernel policy table** → rejected — a hidden
    kernel allowlist makes "what can a person do" unreadable. Power
    must be visible on the objects that carry it: read the devfs/disk
    node's ACL or read the helper (§4.4).
11. **Setuid 0 as such** → resolved: not the problem; the danger is
    the bit on *generic or influencable* binaries (shells, toybox,
    doas, anything that execs or edits on the caller's say-so). A
    narrow, closed, first-party, non-spawning, System-owned helper
    has none of those properties. The keystone (§6) is therefore
    stated about shells, not about uid 0 in general.
12. **`disk` needs a helper** → resolved against: raw block access is
    object-shaped — a role-group ACL on the `@Disk/...` node, format
    tools unprivileged (§4.4).
13. **The session trust boundary** → stated: the interactive Admin
    session is the boundary; a compromised GUI app is a compromised
    Admin (same position every desktop OS takes). Damage control is
    for non-session compromise. Rev-1 §2.1's "never System's" claim
    is scoped to non-session processes.
14. **Service-default polarity** → open, leaning per-service: at FNX
    scale a daemon "zoo" is a handful of records; a shared `Service`
    uid is containment- and audit-blind between daemons. `Service` as
    placeholder for unassigned daemons; dedicated accounts once a
    daemon exists. `Service` reading `Shared/Configuration` by default
    is questioned (Shared is the third-party scope).

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
