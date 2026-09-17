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
        session = ctx.boot()
        ready = session.shell_ready(150)
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

        # --- the mount table's OWN SPELLING, in the guest ---------------
        # system.mounts.conf is an XML plist (P3c-b) and init reads it through
        # libconfig (tools/init.c: mount_from_table is now record enumeration).
        # The mount-table check above proves the mounts happened; THIS proves
        # they came from the CONVERTED file — a working mount table says nothing
        # about which spelling produced it, and a hand parser reading a plist
        # would have mounted nothing while the boot still came up.
        mark = len(session.log_text())
        session.run("cat /System/Configuration/system.mounts.conf")
        conf = session.output_since(mark)
        self.check("mounts-domain-is-a-plist",
                   '<plist version="1.0">' in conf
                   and "<key>processes</key>" in conf
                   and "<string>/System/Processes</string>" in conf
                   and "<key>esp</key>" in conf,
                   "the shipped mount table is the converted plist"
                   if '<plist version="1.0">' in conf
                   else "the guest has the legacy file, or no file: "
                        + conf.strip()[:160])

        # --- the display domain, read by init before the session ---------
        # Same argument as the mount table: this shows the guest has the
        # CONVERTED file, and the check below shows init read it through
        # libconfig. It ships in the SHARED scope (the file's own header says a
        # System-scope copy wins over this default) — which is what the old
        # hand-written two-path loop emulated, and what libconfig's precedence
        # now does for real.
        mark = len(session.log_text())
        session.run("cat /Shared/Configuration/system.display.conf")
        dconf = session.output_since(mark)
        self.check("display-domain-is-a-plist",
                   '<plist version="1.0">' in dconf
                   and "<key>display</key>" in dconf
                   and "<integer>1920</integer>" in dconf,
                   "the shipped display domain is the converted plist"
                   if '<plist version="1.0">' in dconf
                   else "the guest has the legacy file, or no file: "
                        + dconf.strip()[:160])

        # init APPLIED what it read: this line prints the mode the framebuffer
        # reported AFTER the ioctl, so the 1920x1080x32 came from the converted
        # domain through libconfig — the strongest evidence that init's reader
        # is the library, not a parser that happened to see the same numbers.
        log = session.log_text()
        applied = "INIT: display: mode 1920x1080 32bpp" in log
        self.check("init-applied-the-display-domain",
                   applied,
                   "init read display.width/height/bpp through libconfig and "
                   "set the mode" if applied
                   else "no mode-set line for 1920x1080x32: "
                        + " | ".join(ln for ln in log.splitlines()
                                     if "INIT: display" in ln)[:200])

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

        # --- the shell's own job-status path ---------------------------
        # This is where a *bogus wait status* surfaces, and it found one: the
        # kernel's wait4 returned the interrupting signal number instead of
        # -EINTR, so the shell paired the number (SIGCHLD's 17) with an
        # uninitialized status and printed a signal name for a child that
        # exited 0 - intermittently, and it made `devpts-mount-reachable`
        # pass while the console said "Unknown signal".  Loop a few children
        # (the race fires on almost every one) and require a clean report.
        mark = len(session.log_text())
        for _ in range(10):
            session.run("ls %s; echo st=$?" % DEV, secs=60)
        shell = session.output_since(mark)
        statuses = [ln.strip() for ln in shell.splitlines()
                    if ln.strip().startswith("st=")]
        deaths = [ln.strip() for ln in shell.splitlines()
                  if any(name in ln for name in
                         ("Killed", "Stack fault", "Bus error",
                          "Unknown signal", "Segmentation fault",
                          "Floating point", "Illegal instruction"))]
        self.check("shell-status-clean",
                   not deaths and all(s == "st=0" for s in statuses)
                   and bool(statuses),
                   "the shell reports a clean status for every child"
                   if not deaths and all(s == "st=0" for s in statuses)
                   and statuses
                   else "bogus shell report: deaths=%s statuses=%s"
                        % (deaths[:3], statuses[:6]))
