# Toybox → FSH port plan

Status: PLAN — for later execution. Applies to the pinned toybox 0.8.11
submodule (`third_party/toybox`), built multi-call static for FNX by
`tools/mktoybox.sh` (defconfig minus kernel-header applets, plus DHCP).

Scope note: FNX's `init` is its own `userland/tools/init.c` and its shell is
dash (`third_party/dash`) — this plan covers toybox only; the dash and
`userland/tools/init.c` ports are tracked separately with the same mapping.

---

## 1. Findings from the survey

- **Toybox has no path-abstraction layer.** Every filesystem path is a
  literal C string — ~150 sites across `lib/`, `toys/`, `main.c`. There
  is no kconfig switch, no compile-time prefix table, no runtime config
  file for paths (the only path-related CFG is `CFG_TOYBOX_ON_ANDROID`,
  which affects 2 sites).
- **Shared lib code is the highest-leverage target** — a handful of files
  serve every applet:
  - `lib/tty.c:25` `/dev/tty`
  - `lib/xwrap.c:203,205,468` `/dev/null`, `/dev/tty`; `:303` `/proc/self/exe`
    (nommu re-exec); `:839` `/var/run/%s.pid` (pidfile helper)
  - `lib/portability.c:50` `/dev/urandom`; `:136` `/proc/mounts`
    (xgetmountlist — feeds df, stat, mount, umount)
  - `lib/lib.c:1149,1170,1177` `/proc/<pid>/...` (pidof)
  - `lib/env.c:140` `_PATH_DEFPATH` default PATH
  - `main.c:255` `toy_paths[] = {"usr/","bin/","sbin/"}` (install/--long)
- **libc `<paths.h>` is FHS-coded.** `_PATH_DEFPATH`
  (`/usr/local/bin:/usr/bin:/bin:...`) and `_PATH_UTMP` come from musl's
  libc; they must be overridden at the FNX toolchain level, not in toybox.
- **Env-var overrides already exist** and are the model to extend:
  `TMPDIR` (mktemp, sh heredocs), `PATH`, `HOME`, `SHELL`, `FSTAB_FILE`
  (fsck), `MANPATH`, `_`. The FSH port keeps these and adds sensible FNX
  defaults at the toolchain/toybox-default level.
- Categories by volume: `/dev` (34 files, incl. 4 shared-lib sites),
  `/proc` (31 files, incl. shared pidof + mountlist), `/etc` (25 files),
  `/var` (8), `/bin`+`/sbin`+`/usr` (interpreter + PATH defaults),
  `/tmp` (4, mostly already TMPDIR-driven), `/home` (1), `/mnt`+`/boot` (1),
  `/lib` (1, modules), `/usr/share` (man + dhcp script). No `/root`,
  `/opt`, `/srv`, `/media`, `/run` literals exist.

## 2. Strategy

**Centralize the mapping in the fork, replace literals with macros.** Add
a single header, `lib/fnxpaths.h`, defining one macro per FSH path:

```c
/* lib/fnxpaths.h — FNX FSH path table (docs/design/fsh-proposal.md §4) */
#define FNX_PATH_NULL      "/System/Devices/null"
#define FNX_PATH_TTY       "/System/Devices/tty"
#define FNX_PATH_URANDOM   "/System/Devices/urandom"
#define FNX_PATH_MOUNTS    "/System/Processes/mounts"
#define FNX_PATH_FSTAB     "/System/Configuration/fstab"
#define FNX_PATH_PASSWD    "/System/Configuration/passwd"
#define FNX_PATH_SHADOW    "/System/Configuration/shadow"
#define FNX_PATH_GROUP     "/System/Configuration/group"
#define FNX_PATH_RESOLV    "/System/Configuration/resolv.conf"
#define FNX_PATH_PIDDIR    "/System/Variable Data/run"     /* /var/run */
#define FNX_PATH_LOGDIR    "/System/Variable Data/log"     /* /var/log */
#define FNX_PATH_SPOOL     "/System/Variable Data/spool"
#define FNX_PATH_TMP       "/System/Temporary Files"
#define FNX_PATH_PROCSELF  "/System/Processes/self/exe"
#define FNX_PATH_TOOLS     "/System/Tools"
#define FNX_PATH_USERS     "/Users"
#define FNX_PATH_MODULES   "/System/Libraries/modules"
#define FNX_PATH_MAN       "/System/Documentation/man"
#define FNX_PATH_DEV_PREFIX "/System/Devices/"             /* /dev/%s patterns */
#define FNX_PATH_PROCPID   "/System/Processes/%ld"         /* /proc/<pid> patterns */
```

Then:

1. **Shared lib first** (P0): rewrite the 6 files above to use the macros.
   This fixes mountlist, pidof, xwrap device fallbacks, pidfiles, the
   default PATH, and `toybox --long` install paths for every applet at
   once.
2. **Applet-by-applet** (P1): replace literals per the mapping table (§3),
   preferring the env-var paths where they already exist (`TMPDIR`,
   `MANPATH`, `FSTAB_FILE`) and only changing the *defaults*.
3. **Toolchain defaults** (P0.5, outside toybox): build musl for FNX with
   the FSH `_PATH_DEFPATH` (`/System/Tools:/Users/$USER/Applications`,
   Q12) so `env`, `su`, and the shell inherit it without toybox code.
4. **Install layout** (P0.5): `scripts/install.sh` + `scripts/install.c`
   + `main.c:255` change the install dirs from
   `{usr/, bin/, sbin/}` to a single `/System/Tools/` — one tool
   directory, no bin/sbin/usr split (§3.2).
5. **Linter gate** (Q1/§6.1): the FNX packaging linter already scans
   staged ELFs for legacy prefixes (`/bin`, `/sbin`, `/usr`, `/etc`,
   `/lib`, `/var`, `/tmp`, `/dev`, `/proc`, `/home`, `/mnt`). The toybox
   build output must pass it, which makes the port *verifiable*: the
   build fails until every literal above is gone.
6. **Keep the fork small**: touch only the paths that FNX builds (the
   mktoybox.sh applet set); leave `toys/pending/*` (disabled except dhcp)
   and `toys/android/*` for when they're actually built.

## 3. Mapping table (old → FSH)

| Legacy | New | Primary sites |
|---|---|---|
| `/dev/tty`, `/dev/null`, `/dev/urandom` | `/System/Devices/{tty,null,urandom}` | lib/tty.c, lib/xwrap.c, lib/portability.c |
| `/dev/%s` patterns (getty, mdev, init, openvt, fdisk, blkid, ps) | `/System/Devices/%s` | toys/lsb/mount.c, toys/pending/* (deferred) |
| `/dev/kmsg`, `/dev/console`, `/dev/log` | `/System/Devices/...` (kmsg, console, log) | toys/lsb/dmesg.c, toys/pending/syslogd.c |
| `/proc/mounts`, `/proc/filesystems`, `/proc/swaps`, `/proc/partitions`, `/proc/modules`, `/proc/sys/...`, `/proc/<pid>/*`, `/proc/self/exe`, `/proc/net/*` | `/System/Processes/...` | lib/portability.c, lib/lib.c, lib/xwrap.c, mount/umount/df/stat/ps/lsmod/blkid/fdisk/... |
| `/etc/fstab` | `/System/Configuration/fstab` | toys/lsb/mount.c:320, toys/lsb/umount.c:55 (keep `FSTAB_FILE` override) |
| `/etc/passwd`, `/etc/shadow`, `/etc/group`, `/etc/gshadow` | `/System/Configuration/...` | toys/lsb/passwd.c, toys/lsb/su.c, toys/pending/* |
| `/etc/nologin`, `/etc/motd`, `/etc/issue`, `/etc/shells`, `/etc/resolv.conf`, `/etc/sysctl.conf`, `/etc/adjtime`, `/etc/mdev.conf` | `/System/Configuration/...` | login, host, sysctl, hwclock/rtcwake, ... |
| `/var/run/*.pid` | `/System/Variable Data/run/*.pid` | lib/xwrap.c:839 |
| `/var/log/*`, `/var/spool/*`, `/var/lib/misc/*` | `/System/Variable Data/{log,spool,lib}/...` | syslogd, last, crond, dhcpd (mostly deferred) |
| `/tmp` (defaults) | `/System/Temporary Files` (keep `TMPDIR` precedence) | toys/lsb/mktemp.c, crontab, bootchartd |
| `/bin/sh`, `/sbin/init`, `/bin/login` (interpreter defaults) | `/System/Tools/sh`, `/System/Tools/init`, `/System/Tools/login` | toys/other/chroot.c, watch.c, getty (deferred) |
| `PATH=/sbin:/usr/sbin:/bin:/usr/bin` + `_PATH_DEFPATH` | `PATH=/System/Tools:/Users/$USER/Applications` (Q12) | lib/env.c, toys/pending/init.c (deferred), toolchain |
| `/home/%s` (useradd default) | `/Users/%s` | toys/pending/useradd.c:68 (deferred) |
| `/etc/skel` | `/System/User Template/` — the home structure, copied to each new user's home | toys/pending/useradd.c (deferred) |
| `/lib/modules/%s` | `/System/Libraries/modules/%s` | toys/pending/modprobe.c, toys/other/modinfo.c |
| `/usr/share/man` | `/System/Documentation/man` | toys/pending/man.c (keep `MANPATH`) |
| `/usr/share/dhcp/default.script` | `/System/Configuration/dhcp/default.script` | toys/pending/dhcp.c:535 (BUILT) |
| `/etc:/vendor:/usr/share/misc` (lsusb) | `/System/Configuration:/System/Shared/...` | toys/other/lsusb.c:112 |
| `{usr/,bin/,sbin/}` install dirs | `System/Tools/` only | main.c:255, scripts/install.c |

## 4. Work packages

- **P0 — shared lib + core (do first, unlocks everything):**
  `lib/fnxpaths.h` (new), `lib/tty.c`, `lib/xwrap.c`, `lib/portability.c`,
  `lib/lib.c` (pidof), `lib/env.c`, `main.c`. Plus toolchain: FNX musl
  `_PATH_DEFPATH` override; install-layout change to a single
  `System/Tools/`.
- **P0.5 — build/install:** `scripts/install.sh` + `install.c` +
  `mktoybox.sh` install PREFIX → `/System/Tools`; make the §6.1 linter
  part of `mktoybox.sh` so the gate runs on every toybox build.
- **P1 — built applets** (the mktoybox.sh-enabled set), by category:
  1. fs/mount: mount, umount, df, stat, fstype, swapoff (mountlist +
     fstab are shared/P0; per-applet cleanup).
  2. proc/ps: ps, pidof (shared), kill, lsmod, modinfo, blkid, fdisk,
     sysctl, pwdx, pmap.
  3. devices: dmesg, login, mktemp, losetup (deferred? built?),
     devmem, hwclock, rtcwake.
  4. config: host (resolv.conf), sysctl, passwd/su (if built), login.
  5. net: dhcp (default.script — BUILT), ifconfig/ip/netstat
     (`/proc/net/*` → `/System/Processes/net/*`).
- **P2 — deferred** (not in the FNX build): `toys/pending/*` except dhcp,
  `toys/android/*`, and the kernel-header-disabled applets. They get the
  same mapping when/if built; the linter gate will catch them.

## 5. Open items for later

- Device-node naming for `/dev/%s` patterns: FSH decides the Devices tree
  is topology + role aliases (§8 Q3) — applets that open a *specific*
  console/tty must use the role path (`/System/Devices/console`, a serial
  port), not a flat `/dev/tty0` list. Needs a per-applet decision when P1
  touches each.
- `/var/run` → `/System/Variable Data/run`: FSH has no `run` subdir yet —
  add it to the scaffold, or map pidfiles to `Temporary Files` (they are
  ephemeral)? Decision for the FSH/config track.
- `/System/Libraries/modules` vs a dedicated modules home (the FSH
  `System/` has no kernel-module category).
- man pages: `/System/Documentation/man` vs the HTML/PDF split already in
  the notes.

## 6. Acceptance (ties to Q11)

- `mktoybox.sh` builds and the linter gate passes: **zero** legacy path
  strings in the staged toybox ELF.
- `toybox --long`, `mount`, `df`, `ps`, `dmesg`, `dhcp`, `login` work on
  the new tree in the QEMU guest.
- Install layout is `/System/Tools/` only; `ls -a /` still shows exactly
  the five top-level entries after install.
