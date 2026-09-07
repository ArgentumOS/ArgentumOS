# FNX command-line design — the domain-verb suite

Status: **PROPOSED** (design riff, 2026); related to the System/Admin
principal model (`system-admin-principal.md`) and the account/config
plans it builds on.

## 1. The principle

A first-party FNX tool is

```
<domain> <verb> [args]
```

where `<domain>` is an FNX-owned noun — a thing the OS itself owns —
and `<verb>` is one operation on it:

```
config  read / write / delete            (shipped)
acl     get / set / default              (shipped)
disk    mount / umount / format / partition / label
network ping / get / dhcp release / status / scan
account passwd / useradd / groupadd / groupdel
app     install / remove
power   off / reboot
```

Two consequences make this more than naming:

- **Discovery is free**: `<domain>` with no verb prints its verb list
  (as `config` does today). The grammar is uniform; help and docs write
  themselves.
- **Privilege belongs to the verb, never the binary.** A command is
  not "trusted" or "untrusted" as a whole — each verb sits at its own
  privilege tier. `network ping` and `network get` are unprivileged
  (any session may run them); `network dhcp release`, `disk format`,
  `account useradd` are privileged and route through the System helper
  / broker path (see `system-admin-principal.md` §4). The front binary
  stays unprivileged and dispatches; elevation is per-verb underneath.

## 2. Two families of tools

The suite is not a toybox replacement *crusade*. Tools divide by who
they serve:

| family | examples | semantics |
|---|---|---|
| **domain-verb tools** (the OS's nouns) | `config`, `acl`, `disk`, `network`, `account`, `app`, `power` | manipulate OS-owned state; often privileged; speak `.conf` domains and Devices topology paths |
| **plain tools** (the person's verbs) | `ls`, `cp`, `mv`, `grep`, `head`, `sh` | work on the user's files; unprivileged; stay familiar POSIX |

`ls` is not `<domain> <verb>` material — there is no OS-owned noun for
"the files the user is looking at", and forcing it into the grammar
buys consistency at the cost of a generation of muscle memory, with no
security gain (plain tools run unprivileged either way). This is the
FSH origin model applied to commands: OS-owning commands are domain
verbs; person-owning commands stay plain.

## 3. Why domain-verb tools leave toybox

A tool moves out of toybox into the suite when FNX's own semantics
demand it — not for completeness:

- the command must address things by **Devices topology paths**
  (`disk format agfs ATA/Bus0/Disk2`), which validates the argument
  against the device tree rather than treating it as a filesystem path;
- its behavior/config belongs in a **.conf domain** (`system.mounts`,
  `system.passwd`) and must use the mediated, validating writer;
- it is **privileged**, and toybox must never run elevated (a setuid
  toybox is a root shell; see `system-admin-principal.md` §4).

`config` and `acl` earned their way in exactly this way; `mount`'s
privileged half grows into `disk`. The suite grows by necessity, not by
inventory.

## 4. Notes and sharp edges

- **A privileged subcommand inside a shared command** (`network dhcp
  release` under an unprivileged `network`) means the binary routes
  gated verbs to the helper/broker — never that the whole binary gains
  suid and then runs unprivileged verbs elevated. Gate by subcommand.
- **`network get` is a file-writer with a caller-chosen destination** —
  fine unprivileged (Admin may write her own files); it must never be
  reachable through an elevated path.
- The noun list is the *product shape*; several verbs are gated on
  kernel work that does not exist yet (raw ICMP for `ping`, a real TCP
  client + DNS beyond hosts for `get`, DHCP release machinery).
- toybox is 0BSD and unprivileged either way: replacing plain tools is
  a **quality/cohesion** choice, never a security one.

## 5. Seed inventory (proposed nouns and their reason to exist)

A noun earns a tool when it owns state no plain tool should touch raw
(a), when its argument space is an FNX namespace rather than files (b),
or when it is where a `.conf`-owned *setting* lives (c).

### System state (privileged: helper/broker-backed; read verbs unprivileged)

```
boot    list / set-default / kernel.conf    (a) owns /System/ESP: FNX.efi, kernel.conf
                                              (system.kernel), boot entries — nothing else
                                              touches the ESP
service list / start / stop / status        (a) boot-time services (kernel.conf services=);
                                              list/status unprivileged
log     show / tail / clear                 (a) /System/Variable Data logs; clear gated to
                                              the owning writer principal
update  check / apply / rollback            (a) OS + app updates: staged, checksummed,
                                              rollback-aware
backup  snapshot / list / restore           (a) user-data snapshots; restore is the scary verb
```

### Devices and peripherals (mostly unprivileged control of session state)

```
display mode / list / brightness            (b) wraps fb0 IO_FB_GETMODE/SETMODE — speaks modes,
                                              not ioctls
sound   volume / mute / device              (c) mixer state; system-owned, person-adjustable
keyboard layout / list                      (b,c) keymap data lives in System/Shared; layout
                                              is a setting
time    status / set / zone                 (c) clock + timezone in System/Configuration; set
                                              is Admin, status is not
```

### Identity and host state

```
host    name / status                       (c) the system.network hostname verb
user    who / sessions                      the *who's-here* noun — account is management,
                                              user is session state
```

### Trust and delegation — the most FNX-flavored one

```
grant   list / add / remove                 (a) the audit + delegation front end over the ACL
                                              model: `grant list /System --user Admin`, `grant
                                              add $USER --group Admin` — "grants you can see"
                                              (system-admin-principal.md §7) wearing a verb hat
```

### Deliberately family-two (stay plain tools)

`ps`/`kill` (processes are person-visible), `edit`/`cat`/`cp`,
`find`/`grep` — files and search are person verbs.

### Extended attributes and the Haiku/AGFS guardrail

Generic extended attributes are family-two, per-file app metadata
(`xattr get/set/list/remove`), not a domain verb — `acl` is a domain
tool because ACLs are *the* permissions model; xattrs are the opposite.

Guardrail (see the OpenBFS cross-compat work): **the `user.*` /
`system.*` xattr namespace convention must be enforced at the tool, not
as a syscall-layer name gate.** AGFS attributes are free-form names with
a 32-bit type code (`BEOS:TYPE`, media attrs, …) — Haiku carries no
POSIX namespace prefixes, so a hard kernel prefix rule would make every
Haiku attribute invisible/unwritable on a mounted AGFS volume and break
file-type identification for cross-boot files. The kernel therefore
ownership-gates only its own two names
(`system.posix_acl_access`/`system.posix_acl_default`); everything else
stays name-permissive at the syscall layer. The open compat surface is
attribute **type-code preservation** (read foreign attributes with their
type intact; write known names with Haiku-appropriate types) and a
FNX⇄Haiku name mapping (`BEOS:TYPE` and friends) — unexercised by the
cross-boot tests, which never exercised attributes.

## 6. Open items

- The full noun inventory and per-domain verb lists as subsystems
  mature.
- Where the boundary sits for commands that straddle the two families.
- Whether the two families live in different directories
  (`/System/Tools/…`) or share one namespace with a marker.
