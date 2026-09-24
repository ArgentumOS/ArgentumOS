# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSFileManager — F13.14's acceptance.

docs/design/foundation-plan.md §10. The file system as a SERVICE rather than a set of wrappers: three
things make it one — every failure answers NO and fills in an `NSError` whose code is the `errno`
instead of leaving it in a global; attributes come back as a dictionary keyed by Cocoa's names rather
than as a bitfield to decode; and COPY RECURSES, which POSIX cannot do at all.

The probe is `/System/Shared/tests/foundation_filemanager`, ONE unit, importing only
`<Foundation/Foundation.h>` (plus `<unistd.h>` for the `symlink(2)` the link check needs).

  * `fs-default-manager` — `+defaultManager` answers the same object twice;
  * `fs-create-and-list` — a directory made WITH intermediates, files inside it, and the NAMES
                          `-contentsOfDirectoryAtPath:error:` reports;
  * `fs-write-and-size`  — a file written through `NSData`, whose `NSFileSize` is EXACTLY the byte
                          count: the one attribute a caller can check without trusting us, plus the
                          type and a modification date;
  * `fs-move-and-copy`   — a move (source gone, destination the same size) and a copy of a whole
                          DIRECTORY, whose members come back with their contents;
  * `fs-error-channel`   — removing something absent answers NO and fills in an error with a
                          non-empty description: the channel, not a silent zero;
  * `fs-link-and-cwd`    — a symbolic link's target through the service, and a
                          `-changeCurrentDirectoryPath:`/`-currentDirectoryPath` round trip;
  * `fs-contents-at-path` — the file's BYTES, and Apple's one exclusion: a directory answers nil;
  * `fs-contents-equal`  — the whole equality rule: the same file, two identical trees, a tree that
                          differs in a SUBDIRECTORY, different bytes, two links to one target, and a
                          link against its own target (which is where "compares the links themselves"
                          becomes visible);
  * `fs-symbolic-link-door` — `-createSymbolicLinkAtPath:withDestinationPath:error:` makes a link to a
                          target that DOES NOT EXIST, and the target reads back;
  * `fs-copy-refuses-an-existing-destination` — a FIX: the copy used to replace the file it found
                          (O_CREAT|O_TRUNC). It answers NO with EEXIST now, and the assertion that
                          matters is that the file that was already there is STILL THERE, byte for byte;
  * `fs-move-refuses-an-existing-destination` — the same rule for the move, which `rename(2)` would
                          never have enforced, with both items still in place;
  * `fs-copy-of-a-symlink-is-a-link` — a symlink SOURCE is copied AS A LINK, and a DANGLING one is
                          the proof: a copy that followed the link would have nothing to read;
  * `fs-attributes-of-file-system` — the file system's own numbers, with Apple's two traps honoured:
                          the SIZES ARE BYTES and the NUMBER is `st_dev`;
  * `fs-item-attributes-name-the-inode` — `NSFileSystemFileNumber`/`NSFileReferenceCount`/
                          `NSFileDeviceIdentifier` against st_ino/st_nlink/st_dev, and the
                          published-but-never-filled `NSFileCreationDate` ABSENT (this substrate keeps
                          no birth time, and an absent entry is how a file system says so);
  * `fs-a-socket-is-named-and-a-fifo-is-not` — the three missing type values landed, so a socket is
                          named; a FIFO stays unknown because Apple publishes no value for one;
  * `fs-relationship-is-about-locations` — Contains / Same / Other, including the sibling whose name
                          merely PREFIXES the directory's (which a plain `hasPrefix:` would call
                          contained), and ENOENT when a side is missing;
  * `fs-cleanup`         — the tree is gone, which is also the recursive remove's own exercise.

IT WORKS IN A TREE OF ITS OWN MAKING under `/System/Temporary Files` — this system's temp directory,
spelled the FSH way — and removes the whole tree at the end AND at the start, because a probe that
leaves litter behind changes the system it is measuring.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_filemanager"
CHECKS = ("fs-default-manager", "fs-create-and-list", "fs-write-and-size", "fs-move-and-copy",
          "fs-error-channel", "fs-link-and-cwd", "fs-contents-at-path", "fs-contents-equal",
          "fs-symbolic-link-door", "fs-copy-refuses-an-existing-destination",
          "fs-move-refuses-an-existing-destination", "fs-copy-of-a-symlink-is-a-link",
          "fs-attributes-of-file-system", "fs-item-attributes-name-the-inode",
          "fs-a-socket-is-named-and-a-fifo-is-not", "fs-relationship-is-about-locations", "fs-cleanup",
          "temporary-directory-is-the-fsh-path", "temporary-directory-exists")


class Case(BaseCase):
    title = "NSFileManager: the file system as a service"
    tier = "fast"
    # Its filesystem reads are read-only FIXTURES in the image; the host-clean list is a different
    # question (a HOST build has no /System), so it can share a guest like the rest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_filemanager")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-FILEMANAGER-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-FILEMANAGER "):
                self.note(line)

        done = "FOUNDATION-FILEMANAGER DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-FILEMANAGER DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-FILEMANAGER %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-FILEMANAGER RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-FILEMANAGER-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
