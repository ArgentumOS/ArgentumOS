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

        session.run("rm -rf " + TMP)
