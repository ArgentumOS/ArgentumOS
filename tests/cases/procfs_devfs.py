"""procfs, devfs and the devpts mount.

Everything here is ground-truthed against a real boot, because the FSH layout
is not the conventional one: procfs is mounted at `/System/Processes` and devfs
at `/System/Devices` - there is no `/proc` and no `/dev`.

The last check covers the devpts mount (`ls /System/Devices/PTS/pts`).  It used
to panic the kernel (a NULL dereference, `vector 0x0e error=0x00 cr2=0x60`)
because the devpts root inode came back with `fsop == NULL`; the codegen bug
behind that is described in tools/patch_pic_data.py, and the check now proves
the mount is reachable.
"""

import re

from harness import BaseCase

PROC = "/System/Processes"
DEV = "/System/Devices"
TOPOLOGY = ("Audio", "Disk", "Display", "Memory", "PS2", "PTS", "Serial", "TTY")


class Case(BaseCase):
    title = "procfs at /System/Processes, devfs topology, the devpts mount"
    tier = "fast"
    timeout = 420

    def run(self, ctx):
        session = ctx.boot_to_desktop(secs=150)
        ready = session.shell_ready(90)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())

        # --- the mount table ------------------------------------------
        # The kernel's own table (procfs) is the one that includes the root
        # entry; toybox `mount` lists only the non-root mounts.
        mark = len(session.log_text())
        session.run("cat %s/mounts" % PROC)
        mounts = session.output_since(mark)
        self.check("mount-table", "agfs" in mounts and "proc" in mounts
                   and "devpts" in mounts,
                   "the kernel's mount table: root agfs + the boot mounts")
        for line in mounts.strip().splitlines()[:5]:
            self.note(line)

        # --- procfs ---------------------------------------------------
        mark = len(session.log_text())
        session.run("cat %s/version" % PROC)
        self.check("procfs-version", "FNX" in session.output_since(mark),
                   "the kernel banner answers through procfs")

        mark = len(session.log_text())
        session.run("cat %s/self/status" % PROC)
        status = session.output_since(mark)
        self.check("procfs-self-status", "Name:" in status and "Pid:" in status,
                   "/self describes the process doing the reading")

        mark = len(session.log_text())
        session.run("cat %s/meminfo" % PROC)
        self.check("procfs-meminfo", bool(re.search(r"(?i)mem:", session.output_since(mark))),
                   "memory accounting answers")

        mark = len(session.log_text())
        session.run("ls %s" % PROC)
        entries = session.output_since(mark)
        missing = [name for name in ("version", "meminfo", "self", "mounts", "net")
                   if name not in entries]
        self.check("procfs-tree", not missing,
                   "the procfs tree is complete" if not missing
                   else "missing: " + ", ".join(missing))

        # --- devfs ----------------------------------------------------
        mark = len(session.log_text())
        session.run("ls " + DEV)
        listing = session.output_since(mark)
        missing = [name for name in TOPOLOGY if name not in listing]
        self.check("devfs-topology", not missing,
                   "every bus directory is present" if not missing
                   else "missing: " + ", ".join(missing))

        mark = len(session.log_text())
        session.run("ls %s/PTS" % DEV)
        pts = session.output_since(mark)
        self.check("devpts-mounted", "ptmx" in pts,
                   "the pty multiplexer node is present")

        mark = len(session.log_text())
        session.run("head -c 4 %s/Memory/zero | od -An -tx1" % DEV)
        self.check("memory-zero-device",
                   "00 00 00 00" in session.output_since(mark),
                   "reading the zero device gives zeroes")

        # --- the devpts mount, last: this used to panic the kernel ------
        before = len(session.log_text())
        session.run("ls %s/PTS/pts" % DEV, secs=30)
        after = session.output_since(before)
        panicked = "KERNEL EXCEPTION" in after
        self.check("devpts-listing-does-not-panic", not panicked,
                   "the kernel survives listing a mount point under devfs"
                   if not panicked else "PANIC: " + after.strip()[:200])

        # A panic prints neither message, so it must count as unreachable:
        # this check passed once while the guest was dying.
        reachable = ("I/O error" not in after and "No such file" not in after
                     and not panicked)
        self.check("devpts-mount-reachable", reachable,
                   "ls of the devpts mount works" if reachable
                   else "the devpts mount is unreachable: ls said "
                        + after.strip()[:200])
