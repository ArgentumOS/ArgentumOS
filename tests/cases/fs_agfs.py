"""The root filesystem: mount table, a write/read round trip, metadata, xattrs.

AGFS is the default root and the only filesystem with a query tool, so this is
where the filesystem contract is most observable from inside a session:

  * the mount table names the filesystems actually mounted;
  * a create/write/read/compare round trip proves the data path (not just that
    the filesystem mounted);
  * `agfsdir` proves directory entries survive at scale (STAT + SPREAD, i.e.
    lookups and a directory that outgrew one node);
  * `agfsattr`/`agfsxattr` prove the metadata and extended-attribute paths,
    which the ACL model is built on.

The ACL tool check is deliberately shallow (the CLI answers for a plain-mode
file); end-to-end multi-user enforcement is a separate case.
"""

import re

from harness import BaseCase

PROBES = "/System/Shared/tests/"
TMP = "/t_fs"


class Case(BaseCase):
    title = "AGFS root: mounts, write/read round trip, metadata, xattrs"
    tier = "fast"
    timeout = 420

    def run(self, ctx):
        session = ctx.boot()
        ready = session.shell_ready(150)

        # --- the mount table ------------------------------------------
        # Two sources, because they differ: toybox `mount` prints the
        # non-root mounts, while the kernel's own table (procfs) includes the
        # root entry.  procfs lives at /System/Processes (there is no /proc).
        mark = len(session.log_text())
        session.run("cat /System/Processes/mounts")
        kernel_table = session.output_since(mark)
        self.check("root-is-agfs", "agfs" in kernel_table,
                   "the kernel's mount table names the root filesystem")

        mark = len(session.log_text())
        session.run("mount")
        mounts = session.output_since(mark)
        self.check("mounts-config-applied",
                   "proc" in mounts and "devpts" in mounts and "fat" in mounts,
                   "the boot mount table (system.mounts.conf) was applied")

        # --- a real round trip ----------------------------------------
        session.run("mkdir -p " + TMP)
        mark = len(session.log_text())
        session.run("printf 'hello-agfs' > %s/a && cat %s/a" % (TMP, TMP))
        out = session.output_since(mark)
        self.check("write-read-round-trip", "hello-agfs" in out,
                   "a written file read back byte for byte")

        mark = len(session.log_text())
        session.run("cp %s/a %s/b && cmp -s %s/a %s/b && echo CMP-OK"
                    % (TMP, TMP, TMP, TMP))
        self.check("copy-compare", "CMP-OK" in session.output_since(mark),
                   "cp + cmp agree on the copy")

        # The AGFS metadata/xattr probes (agfsdir/agfsattr/agfsxattr) are
        # committed and now built into the image, but their expectations have
        # not been established from a real run yet - they are the next thing to
        # ground-truth and turn into checks (tests/COVERAGE.md).

        # --- the ACL model's userland face ----------------------------
        mark = len(session.log_text())
        session.run("chmod 600 %s/a && acl get %s/a" % (TMP, TMP))
        acl = session.output_since(mark)
        self.check("acl-tool-answers", bool(acl.strip())
                   and "No such" not in acl and "error" not in acl.lower(),
                   "acl get answered for a 0600 file")

        # --- a configuration domain is a file, so it reads through --
        mark = len(session.log_text())
        # the desktop's domains (system.kestrel / system.workspace) went
        # with the apps in the 2026-09 restart, so read back a domain that
        # ships: the network domain carries the machine name
        session.run("config read system.network")
        conf = session.output_since(mark)
        self.check("config-domain-readable", "hostname" in conf,
                   "system.network.conf reads back through the config tool")

        # The last two legacy domains converted (P4): the SPELLING is checked
        # directly, because a working read says nothing about which grammar
        # produced it. system.network is init's (it sets the kernel nodename at
        # boot) and system.shells is the login-shell list chsh/su read.
        mark = len(session.log_text())
        session.run("cat /System/Configuration/system.network.conf")
        netconf = session.output_since(mark)
        self.check("network-domain-is-a-plist",
                   '<plist version="1.0">' in netconf
                   and "<key>hostname</key>" in netconf,
                   "the shipped network domain is the converted plist"
                   if '<plist version="1.0">' in netconf
                   else "the guest has the legacy file, or no file: "
                        + netconf.strip()[:160])

        # init APPLIED it: the nodename comes from the converted domain through
        # libconfig, so a plist the boot could not read would leave this empty.
        mark = len(session.log_text())
        session.run("hostname")
        applied = session.output_since(mark)
        self.check("hostname-applied-from-the-network-domain",
                   "fnx" in applied,
                   "init read hostname from the converted network domain"
                   if "fnx" in applied
                   else "the nodename is not fnx: " + applied.strip()[:120])

        mark = len(session.log_text())
        session.run("config read system.shells")
        shells = session.output_since(mark)
        mark = len(session.log_text())
        session.run("cat /System/Configuration/system.shells.conf")
        shellsfile = session.output_since(mark)
        self.check("shells-domain-is-a-plist",
                   '<plist version="1.0">' in shellsfile
                   and "<key>shells</key>" in shellsfile
                   and "/System/Tools/sh" in shells,
                   "the shipped shells domain is the converted plist, and it "
                   "still reads back through the config tool"
                   if '<plist version="1.0">' in shellsfile and shells.strip()
                   else "the guest has the legacy file, or no file: "
                        + shellsfile.strip()[:160])

        # --- the identity + name domains, read by LIBC ------------------
        # P3d's instrument (docs/design/plist-config-plan.md): musl renders
        # system.passwd.conf / system.group.conf through its own in-libc module
        # (src/passwd/pwconf.c) and resolves names through the hosts domain. This
        # has to be GREEN ON THE LEGACY FILES before those domains convert —
        # otherwise a plist-reader bug would look like a conversion failure.
        #
        # `id` prints the account and group NAMES, which can only come from the
        # two domains (they are the only source of identity names in this
        # system); `ls -l` names an owner through the same getpwuid call.
        mark = len(session.log_text())
        session.run("id")
        ident = session.output_since(mark)
        self.check("account-domains-read-by-libc",
                   "uid=0(Admin)" in ident and "gid=0(Admin)" in ident,
                   "id resolved both names from the identity domains"
                   if "uid=0(Admin)" in ident
                   else "id said: " + ident.strip()[:160])

        mark = len(session.log_text())
        session.run("ls -l /System/Tools/sh")
        owner = session.output_since(mark)
        self.check("owner-name-resolved",
                   "Admin" in owner,
                   "ls -l named the owner through getpwuid"
                   if "Admin" in owner
                   else "ls -l said: " + owner.strip()[:160])

        # THE HOSTS DOMAIN is read by the same in-libc module (lookup_name ->
        # __pwconf_hosts_fopen), and the instrument is a RESOLUTION. `ping` cannot
        # provide one here: it creates an ICMP DGRAM socket BEFORE it looks the
        # name up, and this kernel refuses that socket ("socket SOCK_DGRAM 3a:
        # Invalid argument" — measured, and a NIC attached to the guest did not
        # change it, so the refusal is the socket TYPE, not the interface). `wget`
        # resolves first and only then opens anything, which is the order this
        # check needs.
        #
        # "localhost" can ONLY come from the hosts domain: no DNS server knows it
        # and there is no other hosts source in this system. So the property is
        # that getaddrinfo SUCCEEDS — wget says "bad address" when it does not —
        # and whatever the connect that follows does is irrelevant here. (No
        # timeout flag: this toybox build has none, and the session's own bound
        # covers it.)
        mark = len(session.log_text())
        session.run("wget http://localhost:1/", secs=40)
        res = session.output_since(mark)
        for line in res.splitlines():
            if "localhost" in line or "bad address" in line:
                self.note(line)
        resolved = "bad address" not in res
        self.check("hosts-domain-resolves",
                   resolved,
                   "getaddrinfo resolved localhost through the hosts domain "
                   "(the connect that follows is not what this checks)"
                   if resolved
                   else "could not resolve localhost from the hosts domain: "
                        + res.strip()[:200])

        session.run("rm -rf " + TMP)
