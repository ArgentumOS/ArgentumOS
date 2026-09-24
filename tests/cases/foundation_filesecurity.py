# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSFileSecurity — W8 slice 5's acceptance (foundation-plan.md §60 and §11.6.1 D13).

Apple's own Overview for this class is two sentences, and both are the specification: "A stub class that
encapsulates security information about a file" and "contains no methods of its own. Instead, it is
transparently bridged to CFFileSecurity." The documentation index agrees - exactly ONE member page
(`init?(coder:)`) - and every fact a reader would expect (an owner, a group, a mode, an access control
list, an owner UUID, a group UUID) is published on CFFileSecurity's C surface instead.

This tree has no CoreFoundation at all (no CFFileSecurity, no CFUUID), so the class ships as the
container Apple publishes and NOT with accessors invented onto it. The facts stay reachable where this
system keeps them: NSFileManager's attributes for owner/group/mode, and the kernel's xattr-backed
POSIX-ACL substrate for the ACL. That boundary is registered as §11.6.1 D13 - and the probe ASSERTS it,
asking for each bridged accessor by name and requiring NO, so the row cannot rot into a silence.

The probe is `/System/Shared/tests/foundation_filesecurity`, ONE unit, importing only
`<Foundation/Foundation.h>`. It needs NO FIXTURE: the class touches no file system, and the interesting
half of its surface is what is NOT on it.

  * `fs-is-a-container`          — an instance exists and is this class's;
  * `fs-conforms-as-apple-lists` — the page's "Conforms To" list is NSCoding, NSCopying and
                                 NSSecureCoding, and the object answers `-conformsToProtocol:` to all
                                 three;
  * `fs-is-secure-codeable`      — the secure half answers, which is this tree's standing ruling
                                 (NSCoding.h) rather than this class's private choice;
  * `fs-a-copy-is-its-own-object` — the type's facts are settable through the bridge Apple documents, so
                                 `-copy` hands back its own object rather than a second reference;
  * `fs-codes-and-comes-back`    — an archive naming this class comes back AS this class;
  * `fs-refuses-a-foreign-archive` — and bytes that are not an archive are refused BY RAISING, which is
                                 Apple's own sentence for `+unarchiveObjectWithData:` ("raises an
                                 NSInvalidArgumentException if data is not a valid archive"), while a nil
                                 coder is refused with nil. The first version of this leg expected nil
                                 and the probe aborted: the instrument was wrong, not the library;
  * `fs-the-bridged-accessors-are-absent` — THE BOUNDARY, ASSERTED: the twelve accessors CFFileSecurity
                                 publishes (owner/group/mode/accessControlList/ownerUUID/groupUUID, get
                                 and set) all answer NO, so D13 is machine-checked.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_filesecurity"
CHECKS = ("fs-is-a-container", "fs-conforms-as-apple-lists", "fs-is-secure-codeable",
          "fs-a-copy-is-its-own-object", "fs-codes-and-comes-back",
          "fs-refuses-a-foreign-archive", "fs-the-bridged-accessors-are-absent")


class Case(BaseCase):
    title = "NSFileSecurity: Apple's stub, shipped as one, with the CFFileSecurity boundary asserted"
    tier = "fast"
    # No fixture and no file system work, so it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_filesecurity")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-FILESECURITY-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-FILESECURITY "):
                self.note(line)

        done = "FOUNDATION-FILESECURITY DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-FILESECURITY DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-FILESECURITY %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-FILESECURITY RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-FILESECURITY-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
