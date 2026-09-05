# /System/Devices topology tree (FSH §8 Q3) — design + migration

Status: **DONE**. Kernel + musl + userland consumers all use the
topology paths (commits 23965e2 design, 6bb5e90 kernel devfs topology +
role symlinks, 7da55b3 kernel boot cmdline/tables, and the full musl +
userland migration). Top-level symlinks remain purely as compatibility. Owner decisions: clean break
(flat real nodes are replaced by the topology tree), single-level layout
(topology dirs and the flat-name role symlinks share `/System/Devices/`),
full per-bus disk topology with a `by-identity/` layer, and **all
consumers migrate** to the topology paths this milestone (the top-level
symlinks are pure compatibility).

## Target tree (real nodes)

Drivers register real nodes under bus/role dirs. Prefix dirs are dev-0
`S_IFDIR` nodes created with `devfs_make_dir()` (which creates ancestors).

```
/System/Devices/
    Memory/            mem, kmem, null, port, zero, full, random, urandom
    TTY/               tty (SYSCON 5:0), console (5:1)
    Serial/            Port0..Port3          (serial.c per probed port)
    PTS/               ptmx (clone)          + pts/  (devpts mount target)
    PS2/               Keyboard, Mouse       (kbd.c, psaux.c)
    USB/               Keyboard, Mouse       (usb-kbd / usb-mouse, when present)
    Display/           fb0
    Audio/             dsp
    Disk/
        IDE/           Disk0..Disk3          (ata.c, order = channel*2+drive)
                       + WholeDisk + Partition<k> under each DiskN
        AHCI/          Disk0..               (ahci.c)
        SCSI/          Disk0..               (pvscsi.c)
        USB/           Disk0..               (usb-storage, via device table)
        NVMe/          Disk0..               (nvme.c)
        Floppy/        Disk0                 (floppy.c)
        RAM/           Disk0                 (ramdisk.c)
        by-identity/   symlinks: ide-Disk0 -> ../IDE/Disk0, ahci-Disk0,
                       scsi-Disk0, usb-storage-Disk0, nvme-Disk0, ...
                       (bus-unit identity; driver model/serial when added)
```

The bus attribution comes from the device-table `struct device .name`
("ide0"/"ide1", "ahci", "pvscsi", "usb-storage", "nvme0", "ramdisk",
"floppy") and each block driver's own probe order — never from the
shared major number.

Each DiskN is a container directory: the whole-disk device lives at
Disk/<bus>/DiskN/WholeDisk (minor 0) and — once a partition table is
scanned (docs/partition-support-plan.md, M2+) — partitions at
Disk/<bus>/DiskN/Partition<k>. Identity links are
Disk/by-identity/<bus>-DiskN -> .../DiskN/WholeDisk and
<bus>-DiskN P<k> -> .../DiskN/Partition<k>.

## Top-level role symlinks (compatibility, all current names resolve)

```
console  -> TTY/console        tty   -> TTY/tty
ttyS0..3 -> Serial/Port0..3    ptmx  -> PTS/ptmx
kbd      -> PS2/Keyboard       mouse -> PS2/Mouse     psaux -> PS2/Mouse
fb0      -> Display/fb0        dsp   -> Audio/dsp
hda..hdd -> Disk/IDE/Disk0..3/WholeDisk
sda..    -> Disk/<AHCI|SCSI|USB>/Disk<n>/WholeDisk  (resolved at probe
            time to the bus that actually registered the unit; sd letters
            keep their meaning only while the old name is still referenced)
fd0      -> Disk/Floppy/Disk0/WholeDisk
ram0     -> Disk/RAM/Disk0/WholeDisk
nvme0n1  -> Disk/NVMe/Disk0/WholeDisk
mem kmem null port zero full random urandom -> Memory/<same>
```

Implementation: the `devfs_aliases()` boot table in `fs/devfs/super.c`
grows to a static symlink table; block-disk symlinks are created by the
per-bus registration when each unit appears (they cannot be static
because the bus/unit mapping is only known at probe time).

## Consumers to migrate (this milestone)

1. Kernel boot: `console=` / `root=` resolution table in
   kernel/multiboot.c now accepts the topology paths (Serial/PortN,
   TTY/console, TTY/tty, Disk/AHCI|SCSI|USB|IDE|NVMe|Floppy|RAM/Disk0)
   mapped to the same device numbers; the EFI `kreal64.c` cmdline is now
   `console=/System/Devices/Serial/Port0
   root=/System/Devices/Disk/AHCI/Disk0`; `kernel/init.c init_console_dev`
   is `/System/Devices/TTY/console`. DONE.
2. musl fsh patch (regen): `/System/Devices/{null,tty,console,ptmx,pts/N}`
   → `/System/Devices/{Memory/null,TTY/tty,TTY/console,PTS/ptmx,
   PTS/pts/N}` and any `/System/Devices/log` handling.
3. devpts boot mount: `system.mounts.conf` target
   `/System/Devices/pts` → `/System/Devices/PTS/pts`; the pty driver
   registers `PTS/pts` as the mount dir. DONE (verified: devpts shows on
   `/System/Devices/PTS/pts`).
4. Userland: Xfb (Display/fb0 + PS2/* + Memory/urandom|null), compositor
   (Display/fb0, PS2), init trampoline (`/System/Devices/TTY/console`),
   audio/pty tools, musl fsh patch (`Memory/null`, `TTY/tty`,
   `TTY/console`, `PTS/ptmx`, `PTS/pts/N`), `system.mounts.conf` target,
   `GUI_MOUSE` seam (`Serial/Port1`). DONE.

## Verification

Guest: `ls -R /System/Devices` shows the topology tree; every role
symlink resolves (`cat /System/Devices/null` etc.); the kernel boots
with topology `root=`/`console=`; `mount`/`config` and Xfb-style opens
work through topology paths; M6b disk test mounts by the topology path.
