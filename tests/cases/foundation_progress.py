# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSProgress — F13.20's acceptance, and THE LAST NAME IN §10'S MECHANISM TABLE.

docs/design/foundation-plan.md §10. It is a TREE, and the tree is the point: a parent reports its own
work plus a SHARE of each child's, where the share is what the child was given with
`-becomeCurrentWithPendingUnitCount:` or `-addChild:withPendingUnitCount:`. The probe is
`/System/Shared/tests/foundation_progress`, ONE unit, importing only `<foundation/Foundation.h>`.

  * `progress-fraction`                  — the count, the fraction and `-isFinished`;
  * `progress-finished`                  — reaching the total finishes it;
  * `progress-child-attaches-to-current` — a child created while a progress is CURRENT attaches to it,
                                         and `+currentProgress` is nil again after `-resignCurrent`;
  * `progress-child-scales-into-parent`  — THE CHECK THAT EARNS ITS PLACE: the parent's own 10 plus
                                         HALF of the 50 its child stands for is 35 — scaling, which no
                                         constant in the probe could produce;
  * `progress-child-completed-share`     — a finished child contributes its whole share;
  * `progress-share-is-the-ceiling`      — a child that OVER-reports contributes its share and no
                                         more, so a parent cannot be pushed past what it expected;
  * `progress-cancel-propagates`         — cancelling a parent cancels its children;
  * `progress-pause-resume`              — the pause flags, with pausability alongside;
  * `progress-kind-and-userinfo`         — kind, description, and user info that can be REMOVED by
                                         setting nil for a key;
  * `progress-zero-total`                — a total of zero is never finished and never divides.

NAMED ABSENT, in the header: `-publish`/`-unpublish` and the subscriber doors (they need a reporting
coordinator), `-cancellationHandler` (no blocks in this library's public headers), and
`-estimatedTimeRemaining`/`-throughput`.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_progress"
CHECKS = ("progress-fraction", "progress-finished", "progress-child-attaches-to-current",
          "progress-child-scales-into-parent", "progress-child-completed-share",
          "progress-share-is-the-ceiling", "progress-cancel-propagates",
          "progress-pause-resume", "progress-kind-and-userinfo", "progress-zero-total")


class Case(BaseCase):
    title = "NSProgress: how much of a job is done, and who is doing it"
    tier = "fast"
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_progress")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-PROGRESS-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-PROGRESS "):
                self.note(line)

        done = "FOUNDATION-PROGRESS DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-PROGRESS DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-PROGRESS %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-PROGRESS RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-PROGRESS-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
