# Argentum for Linux users

A quick orientation to Argentum for people who know Linux well and
Argentum not at all. It assumes you can boot it (QEMU or hardware) and
sit at a shell or the desktop. Everything below is current unless
marked *(direction)* — planned, not built yet.

## The one-paragraph difference

Linux is a kernel with adopted conventions; Argentum is a whole OS
built on its own choices: its own filesystem hierarchy (FSH), its own
config format (`.conf`), its own native filesystem (AGFS), its own
GUI stack — while keeping enough POSIX that familiar commands and C
programs work. You will recognize almost every *tool*; the *layout*
and the *way things are administered* are where you'll stumble.

## Filesystem map (the first thing to unlearn)

| Linux habit | Argentum reality |
|---|---|
| `/bin`, `/sbin`, `/usr/bin` | `/System/Tools` (toybox applets + native tools; `sh` lives here too) |
| `/etc` config files | `/System/Configuration/*.conf` — one grammar (`.conf`), edited by the `config` tool, never by hand-editing |
| `/etc/fstab` | `system.mounts.conf` (a `.conf` domain) |
| `/dev` | `/System/Devices` — a *topology tree*: `@Disk/`, `@TTY/`, `@Serial/`, … `@` anywhere is shorthand for `/System/Devices` (`ls @`, `2>@null`, `@Disk/by-identity/…`) |
| `/home/user`, `/root` | `/Users/<name>` (one tree for people) |
| `/usr/share`, `/usr/lib`, app data | `/Shared` (third-party content) and `/System` (system content); per-user data under `/Users/$USER` |
| `/tmp`, logs, state | `/System/Variable Data` (the writable-by-design state area) |
| `/proc` | `/System/Processes` (a procfs-analog) |
| `/mnt`, `/media` | `/Volumes` (mount points live here) |
| `/opt`, `/usr/local`, apt packages | `/Applications` + `~/Applications` — see Software below |
| dotfiles (`~/.bashrc`, `~/.config`) | no dotfiles: settings are `.conf` domains, behaviour (scripts) is under `Application Support/`, history under `Variable Data/` |

## The shell

Today `sh` is **dash** (ash-derived): POSIX, fast, and deliberately
*without* bashisms (`[[ ]]`, arrays, `source` — use `.`, `${var,,}`
won't work). Everything in this guide assumes POSIX sh. *(Direction:
an interactive shell named `finch` with a designed language — rc
semantics, C-style syntax — is planned as the human shell; dash stays
`/bin/sh` for scripts forever. Don't write scripts against finch.)*

## Users, root, and permissions

- **There is no `root` account you log into, and no `sudo` or `su`.**
  This is deliberate. *(Direction: the principal model — `System`
  (uid 0, the machine), `Service`/`Display` (unprivileged daemons and
  the display), and people who belong to the `Admin` group when they
  administer.)* Today everything effectively runs as the single
  `Admin` account.
- Privileged operations happen through **narrow helper commands** you
  run as yourself: `power off` / `power reboot`, `account <user>
  …`, `group <name> create|delete`, `disk mount/unmount/…`,
  `install <bundle> <global|local>`, `config` for system domains.
  They check who you are; there's no way to "become root."
- Permissions are **POSIX ACLs** — richer than mode bits (named
  users/groups, a mask). `ls -l` shows a mode projection; `acl get
  <path>` shows the real model. `chmod` edits the ACL's entries.

## Configuration

All configuration — system, apps, even the kernel's boot config — is
the same `.conf` grammar (libconfig). Key domains live in
`/System/Configuration/`: `system.mounts.conf`, `system.passwd.conf`,
`system.group.conf`, `system.kernel.conf` (the bootloader's config on
the ESP), and per-user overrides under `Users/$USER/Configuration/`.
Use `config` to inspect/change domains:
```
config get system.mounts
config set system.network.hostname = "mybox"
```
Precedence: system defaults → shared → per-user. Don't hand-edit;
`config` validates and writes atomically. *(No `/etc`, no
`sysctl.conf`, no init scripts — that's the point.)*

## Software (no package manager, no apt)

Software is **bundles** — a directory with a `.conf` manifest
(`<Name>.app`, or shared-content bundles for libraries/fonts). To
install:
```
install MyApp.app global     # /Applications (Admin-gated)
install MyApp.app local      # ~/Applications (your own, no elevation)
uninstall MyApp global
```
The file manager's drag-and-drop does this for you *(desktop-era)*.
There is no repository and no dependency resolver — bundles are
self-contained, validated at install (a linter rejects anything
carrying legacy Linux paths), and system-owned after install.
Command-line tools come from the toybox multicall binary (busybox's
permissive sibling) plus FNX-native tools.

## Disks, filesystems, mounts

- **AGFS** is the native filesystem: journaled, crash-tested, with
  per-file attributes and a query engine (and, on the roadmap:
  versioning with Time-Machine semantics, compression, more). Other
  filesystems mount read/write as interop: ext2, FAT32, minix,
  ISO9660 (+Rock Ridge). *(Direction: ExFAT, UDF.)*
- Devices appear under `@Disk/…` by bus (`@Disk/AHCI/Disk0`) with
  `by-identity` links. `disk list` shows everything; `disk info
  @Disk/…` inspects; `disk mount <device|guid> <mountpoint>` mounts
  into `/Volumes`; `disk partition create <device> <type> <size>`
  creates GPT partitions (`type` = `agfs|swap|esp`, mapped to the
  reserved GUIDs).
- Boot-time mounts come from `system.mounts.conf`; runtime mounts are
  the `disk` helper's job.

## Processes and services

- `ps` (toybox) and `/System/Processes` work as you'd expect; kill,
  `wait`, job control all exist.
- *(Direction: services — daemons like the X server — are declared in
  a `system.services` domain, supervised by init itself, and managed
  with a `service <name> start|stop|restart|status` command. There is
  no systemd and nothing like it.)*

## The desktop

The graphical session is the **Argentum Desktop**: an X11 server
(Xfb) rendering to the framebuffer, X clients over `:0`. It's the one
graphics path — no compositor. *(Direction: the Argentum UIKit
toolkit, Workspace file manager + dock, and the shell above are being
built on it.)* If you're used to X11, X clients run unmodified; the
"desktop" is where Argentum's own apps will appear.

## Quick-reference gotchas

- `sh` is dash, not bash. Scripts should be POSIX.
- Paths are `@`-shorthand-able and topology-shaped, not
  `/dev/sdX`-shaped: `ls @Disk`.
- There is no `root`/`sudo`/`su`; use the helpers (`power`,
  `account`, `disk`, `install`, `config`).
- Don't hand-edit config; `config` is the door.
- No dotfiles, no `/etc`: your state lives under
  `/System/Variable Data` and `/Users/$USER`.
- Linux desktop habits that don't exist yet *(direction)*: a package
  manager, `systemd`-style units, cron/scheduling, printing, cups —
  each has a documented plan (thin spooler, services domain, …) but
  isn't built. When in doubt, `ls @`, read the `.conf` domains, and
  check `docs/` in the source tree — the design documents are the
  manual.
