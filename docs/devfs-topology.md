# /System/Devices topology tree (FSH §8 Q3) — design + migration

Status: **in progress**. Owner decisions: clean break (flat real nodes are
replaced by the topology tree), single-level layout (topology dirs and the
flat-name role symlinks share `/System/Devices/`), full per-bus disk
topology with a `by-identity/` layer, and **all consumers migrate** to the
topology paths this milestone (the top-level symlinks are pure
compatibility).

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
shared major number. Partition nodes are not created (FNX has no
partition layer; disks are whole-disk filesystems).

## Top-level role symlinks (compatibility, all current names resolve)

```
console  -> TTY/console        tty   -> TTY/tty
ttyS0..3 -> Serial/Port0..3    ptmx  -> PTS/ptmx
kbd      -> PS2/Keyboard       mouse -> PS2/Mouse     psaux -> PS2/Mouse
fb0      -> Display/fb0        dsp   -> Audio/dsp
hda..hdd -> Disk/IDE/Disk0..3
sda..    -> Disk/<AHCI|SCSI|USB>/Disk<n>   (resolved at probe time to the
            bus that actually registered the unit; sd letters keep their
            meaning only while the old name is still referenced)
fd0      -> Disk/Floppy/Disk0  ram0  -> Disk/RAM/Disk0  nvme0n1 -> Disk/NVMe/Disk0
mem kmem null port zero full random urandom -> Memory/<same>
```

Implementation: the `devfs_aliases()` boot table in `fs/devfs/super.c`
grows to a static symlink table; block-disk symlinks are created by the
per-bus registration when each unit appears (they cannot be static
because the bus/unit mapping is only known at probe time).

## Consumers to migrate (this milestone)

1. Kernel boot: `console=` / `root=` resolution table in
   kernel/multiboot.c + the EFI `kreal64.c` cmdline
   (`root=/System/Devices/Disk/AHCI/Disk0`, `console=/System/Devices/
   Serial/Port0`), `kernel/init.c init_console_dev`, the `/dev/root`
   pseudo mount-point label kept internal.
2. musl fsh patch (regen): `/System/Devices/{null,tty,console,ptmx,pts/N}`
   → `/System/Devices/{Memory/null,TTY/tty,TTY/console,PTS/ptmx,
   PTS/pts/N}` and any `/System/Devices/log` handling.
3. devpts boot mount: `system.mounts.conf` target
   `/System/Devices/pts` → `/System/Devices/PTS/pts`; init's
   `mount_from_table` default file unchanged (target comes from the
   domain); the pty driver registers `PTS/pts` as the mount dir.
4. Userland: Xfb (fb0 + PS2 kbd/mouse + null/urandom), compositor
   (fb0, PS2), init trampoline (`/System/Devices/console`), audio/pty
   test tools, the guest harnesses' `ls /System/Devices` expectations,
   `tools/kernel-headers`/docs path references.

## Verification

Guest: `ls -R /System/Devices` shows the topology tree; every role
symlink resolves (`cat /System/Devices/null` etc.); the kernel boots
with topology `root=`/`console=`; `mount`/`config` and Xfb-style opens
work through topology paths; M6b disk test mounts by the topology path.
