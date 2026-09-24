# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSFileAccessIntent + the coordinator option sets — W8 slice 7a's acceptance (foundation-plan.md §60).

The coordinator family is ~37 published members (13 on NSFileCoordinator, 24 on NSFilePresenter), so it
lands as slices, and this one is the VOCABULARY its operations are spelled with: the value object that
carries "the details of a coordinated-read or coordinated-write operation" (Apple's abstract) plus the two
option sets.

Apple publishes TWO FACTORIES AND `-URL` for this class and NOTHING else — measured from its page — so no
options accessor and no kind accessor is invented, and the probe asserts that absence rather than leaving it
to be discovered. The two option sets are Apple's names with our values (§11.6.1 D2), distinct bits so a
caller can combine them.

The probe is `/System/Shared/tests/foundation_fileaccessintent`, ONE unit, importing only
`<Foundation/Foundation.h>`. It needs NO FIXTURE: this slice is pure vocabulary and touches no file system.

  * `fai-a-reading-intent-carries-its-url` — the reading factory answers an object whose `-URL` is the URL
                                 it was given;
  * `fai-a-writing-intent-carries-its-url` — and the writing factory does the same;
  * `fai-the-two-kinds-are-different-objects` — the kind is what the two factories differ by, so two
                                 intents for one URL are never the same object;
  * `fai-a-nil-url-is-refused` — an intent with no item is refused where it is MADE rather than carried
                                 until the door that would use it;
  * `fai-the-options-are-distinct-bits` — the nine published members are non-zero, pairwise distinct and
                                 combinable;
  * `fai-there-is-no-options-accessor` — THE BOUNDARY, ASSERTED: no accessor Apple does not publish exists
                                 on the class.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_fileaccessintent"
CHECKS = ("fai-a-reading-intent-carries-its-url", "fai-a-writing-intent-carries-its-url",
          "fai-the-two-kinds-are-different-objects", "fai-a-nil-url-is-refused",
          "fai-the-options-are-distinct-bits", "fai-there-is-no-options-accessor")


class Case(BaseCase):
    title = "NSFileAccessIntent: the coordinator family's vocabulary"
    tier = "fast"
    # No fixture, no file system work: it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_fileaccessintent")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-FILEACCESSINTENT-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-FILEACCESSINTENT "):
                self.note(line)

        done = "FOUNDATION-FILEACCESSINTENT DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-FILEACCESSINTENT DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-FILEACCESSINTENT %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-FILEACCESSINTENT RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-FILEACCESSINTENT-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
