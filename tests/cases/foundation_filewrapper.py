# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSFileWrapper — W8 slice 4's acceptance (foundation-plan.md §60).

A file-system node as an OBJECT: a regular file with its bytes, a directory with a dictionary of
children keyed by a unique filename, or a symbolic link with a destination. Reading builds the tree from
disk, writing puts it back, and everything between is a value operation.

The probe is `/System/Shared/tests/foundation_filewrapper`, ONE unit, importing only
`<Foundation/Foundation.h>`.

  * `fw-wraps-a-file`            — the kind is decided "by the type of file-system node", and a regular
                                 file's wrapper carries its bytes, its name and its attributes;
  * `fw-wraps-a-directory-tree`  — a directory is a dictionary of children keyed by name, navigable;
  * `fw-wraps-a-link`            — a link is a link and NOT what it points at (lstat, not stat), which
                                 is what lets a DANGLING one be wrapped at all;
  * `fw-writing-reproduces-the-tree` — a write is recursive and puts back what was read;
  * `fw-the-reading-option-is-observable` — `NSFileWrapperReadingImmediate` is "the option to read files
                                 immediately", so its absence is the LAZY form, and the two differ
                                 exactly when the file changes between wrapping and reading;
  * `fw-add-file-wrapper-answers-a-unique-key` — the key is the child's preferred name "unless that name
                                 is already in use";
  * `fw-a-second-child-with-one-name-gets-another-key` — the same rule from the caller's side, with
                                 `-removeFileWrapper:` taking one back out;
  * `fw-adding-to-a-non-directory-raises` — Apple raises rather than doing nothing quietly;
  * `fw-a-new-wrapper-has-no-filename-yet` / `fw-atomic-writing-leaves-no-temporary` — `-filename` is nil
                                 until a write asks for the name updating ("descendant file wrappers'
                                 properties are set if the writing succeeds"), and an atomic TREE write
                                 lands whole and leaves its temporary name behind for nobody;
  * `fw-unchanged-contents-are-linked-and-changed-ones-copied` — `originalContentsURL` is for not
                                 rewriting what did not change: identical bytes are LINKED into place
                                 (the same inode) while changed bytes are copied;
  * `probe-tree-removed`         — the tree is gone.

IT WORKS IN A TREE OF ITS OWN MAKING under `/System/Temporary Files` and removes it at the end AND at
the start.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_filewrapper"
CHECKS = ("fw-wraps-a-file", "fw-wraps-a-directory-tree", "fw-wraps-a-link",
          "fw-writing-reproduces-the-tree", "fw-the-reading-option-is-observable",
          "fw-add-file-wrapper-answers-a-unique-key",
          "fw-a-second-child-with-one-name-gets-another-key",
          "fw-adding-to-a-non-directory-raises", "fw-a-new-wrapper-has-no-filename-yet",
          "fw-atomic-writing-leaves-no-temporary",
          "fw-unchanged-contents-are-linked-and-changed-ones-copied", "probe-tree-removed")


class Case(BaseCase):
    title = "NSFileWrapper: the tree as an object, read, walked and written back"
    tier = "fast"
    # It builds and removes a tree of its own under the temp directory, so it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_filewrapper")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-FILEWRAPPER-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-FILEWRAPPER "):
                self.note(line)

        done = "FOUNDATION-FILEWRAPPER DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-FILEWRAPPER DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-FILEWRAPPER %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-FILEWRAPPER RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-FILEWRAPPER-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
