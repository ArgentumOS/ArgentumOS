# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSFileCoordinator's synchronous accessor doors — W8 slice 7b's acceptance (foundation-plan.md §60).

Apple's own words for what a coordinated operation is, measured from the doors' page: "the actual URL
passed to the [accessor] may be DIFFERENT than the one in this parameter ... ALWAYS USE THE URL PASSED INTO
THE BLOCK instead of the version you provided". And the error shape is the other half, verbatim: "If a file
presenter encounters an error while preparing for this read operation, that error is returned in this
parameter and the block in the [accessor] parameter IS NOT EXECUTED" — which is why the doors return
`void`, report through `outError`, and why every refusal here is asserted by COUNTING the accessor's calls
as well as by reading the error.

THE BOUNDARY, AT THE DOORS BECAUSE IT IS WHAT THEY MEAN HERE: this system has NO coordination service and
no file provider, so what is coordinated is what happens WITHIN one process — the accessor runs and (7c)
the presenters registered in this process are informed. Cross-process coordination is not implied, and the
probe asserts that the doors this slice does not ship do not exist.

The probe is `/System/Shared/tests/foundation_filecoordinator`, ONE unit, importing only
`<Foundation/Foundation.h>` plus the POSIX calls its fixture makes.

  * `coordinator-runs-the-read-accessor-once` — the accessor runs exactly once, gets the item's URL, and the
                                 caller's out-error stays nil;
  * `coordinator-runs-the-write-accessor-once` — the writing door has the same shape;
  * `coordinator-runs-a-read-and-a-write-in-one-accessor` / `coordinator-runs-two-writes-in-one-accessor`
                                 — the 2-item forms hand the accessor BOTH URLs, once;
  * `coordinator-resolves-a-symbolic-link-when-asked` — `NSFileCoordinatorReadingResolvesSymbolicLink`,
                                 asserted FROM BOTH SIDES: with it the accessor sees the resolved item,
                                 without it the link itself;
  * `coordinator-refuses-without-running-the-accessor` — the error is set and the block does NOT execute
                                 (counted); a nil URL and a non-file URL are both EINVAL;
  * `coordinator-refuses-a-missing-accessor` — a nil accessor is refused, not invoked;
  * `coordinator-owes-the-asynchronous-and-presenter-doors` — THE BOUNDARY, ASSERTED: the doors this slice
                                 does not ship do not exist on the object;
  * `probe-tree-removed` — the tree is gone.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_filecoordinator"
CHECKS = ("coordinator-runs-the-read-accessor-once", "coordinator-runs-the-write-accessor-once",
          "coordinator-runs-a-read-and-a-write-in-one-accessor",
          "coordinator-runs-two-writes-in-one-accessor",
          "coordinator-resolves-a-symbolic-link-when-asked",
          "coordinator-refuses-without-running-the-accessor",
          "coordinator-refuses-a-missing-accessor",
          "coordinator-owes-the-asynchronous-and-presenter-doors", "probe-tree-removed")


class Case(BaseCase):
    title = "NSFileCoordinator: the synchronous accessor doors, in-process"
    tier = "fast"
    # It builds and removes a tree of its own under the temp directory, so it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_filecoordinator")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-FILECOORDINATOR-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-FILECOORDINATOR "):
                self.note(line)

        done = "FOUNDATION-FILECOORDINATOR DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-FILECOORDINATOR DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-FILECOORDINATOR %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-FILECOORDINATOR RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-FILECOORDINATOR-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
