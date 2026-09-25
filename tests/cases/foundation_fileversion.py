# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSFileVersion — W8 slice 8a's acceptance (foundation-plan.md §60).

THE CLASS IS WRITTEN AROUND A VERSION STORE, WHICH THIS SYSTEM DOES NOT HAVE, so this slice is built around
the difference between what a store is needed for and what it is not. The CURRENT version of an item is a
fact about a file (its URL, its name, its date, whether its contents are local); the collections that ask for
versions OTHER than the current one are TRUTHFULLY EMPTY, because nothing on this system stores them — an
empty array is the postcondition and not a euphemism; and the doors that would need somewhere to PUT a
version are REFUSED BY NAME, with an error where Apple gives the door one and nil where it does not.

Two choices are ours and are written at the declarations: `-localizedName` is the item's own name (no
localisation database here — the rule `-displayNameAtPath:` and `NSURLLocalizedNameKey` already follow), and
`-persistentIdentifier` is an opaque string this class defines whose one guarantee is Apple's own ("can be
used to refer to this version in the future") — which the probe checks by ROUND TRIPPING it. And the conflict
machinery (`-isResolved`, `-localizedNameOfSavingComputer`, `-isDiscardable`) is NOT declared: a getter that
answered YES or NO to "is this conflict resolved" would be inventing a fact.

The probe is `/System/Shared/tests/foundation_fileversion`, ONE unit, importing only
`<Foundation/Foundation.h>` plus the POSIX calls its fixture makes.

  * `version-the-current-version-describes-the-item` — its URL, name, date (through TWO doors: the
                                 version's and NSFileManager's) and what is local about it;
  * `version-a-missing-item-has-no-current-version` — nil, which is what Apple's nullable return allows;
  * `version-there-are-no-other-versions` — an EMPTY ARRAY and not nil;
  * `version-the-identifier-round-trips` — what this class handed out can be handed back, and anything else
                                 is answered nil rather than matched loosely;
  * `version-there-are-no-conflicts` — the conflict collection is empty for the same reason;
  * `version-removing-other-versions-is-already-true` — the postcondition holds, so the door succeeds;
  * `version-the-store-doors-refuse-by-name` — adding, removing THIS version and staging a new one are all
                                 refused with an error (or nil where Apple gives no error parameter);
  * `version-nonlocal-versions-answer-empty` — the completion handler IS CALLED, with an empty array: the
                                 question was asked and answered rather than left hanging;
  * `probe-tree-removed` — the tree is gone.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_fileversion"
CHECKS = ("version-the-current-version-describes-the-item",
          "version-a-missing-item-has-no-current-version", "version-there-are-no-other-versions",
          "version-the-identifier-round-trips", "version-there-are-no-conflicts",
          "version-removing-other-versions-is-already-true",
          "version-the-store-doors-refuse-by-name", "version-nonlocal-versions-answer-empty",
          "probe-tree-removed")


class Case(BaseCase):
    title = "NSFileVersion: the current version, the empty collections, and the store refused by name"
    tier = "fast"
    # It builds and removes a tree of its own under the temp directory, so it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_fileversion")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-FILEVERSION-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-FILEVERSION "):
                self.note(line)

        done = "FOUNDATION-FILEVERSION DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-FILEVERSION DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-FILEVERSION %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-FILEVERSION RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-FILEVERSION-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
