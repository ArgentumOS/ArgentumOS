# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSBackgroundActivityScheduler — §62.66's acceptance.

docs/design/foundation-plan.md §62.66. Apple-deprecated and shipping anyway, because the user's decision of
2026-09-26 (§62.24) is that deprecated API is a PORTING TARGET rather than an exclusion.

The class is an activity that runs a block, hands it a COMPLETION HANDLER, and decides what happens next from
the answer — finish, defer, or (for a repeating activity) run again. The engine is ours and runs on the run loop
that already ships: one one-shot timer at `-interval`, rescheduled by the answer.

The probe is `/System/Shared/tests/foundation_backgroundactivity`, ONE unit, importing only
`<Foundation/Foundation.h>`.

  * `the-identifier-is-kept-and-an-empty-one-is-refused` — Apple's rule enforced at the door: "nil and
                                     zero-length strings are not allowed";
  * `the-defaults-are-apples-two-and-ours-for-the-rest` — `NSQualityOfServiceBackground` and non-repeating are
                                     Apple's own defaults; `-interval` and `-tolerance` start at 0 as ours;
  * `the-properties-round-trip` — including the two that are KEPT AND REPORTED rather than obeyed;
  * `a-block-runs-on-the-loop-and-receives-a-completion-handler` — and the handler is what the block answers;
  * `finishing-a-non-repeating-activity-stops-it` — an EXACT count, because no interval can make it run again;
  * `deferring-brings-the-activity-back` — "not finished", whatever `-repeats` says;
  * `a-repeating-activity-runs-again-after-finishing` — the other half of the answer;
  * `invalidate-stops-future-invocations` — asserted as "the count STOPS GROWING" against an activity that
                                     defers, which is the only shape a timing-dependent number cannot fake.

THREE CHECKS READ A GROWTH RATHER THAN A NUMBER, deliberately: how many times a deferred or repeating activity
runs inside a window is the interval's business and not this class's, so the checks assert the INVARIANT (it
came back; it ran exactly once; it stopped).
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_backgroundactivity"
CHECKS = ("the-identifier-is-kept-and-an-empty-one-is-refused",
          "the-defaults-are-apples-two-and-ours-for-the-rest",
          "the-properties-round-trip",
          "a-block-runs-on-the-loop-and-receives-a-completion-handler",
          "finishing-a-non-repeating-activity-stops-it",
          "deferring-brings-the-activity-back",
          "a-repeating-activity-runs-again-after-finishing",
          "invalidate-stops-future-invocations")


class Case(BaseCase):
    title = "NSBackgroundActivityScheduler: an activity, its answer, and the run loop"
    tier = "fast"
    # No fixture and no file system work: it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_backgroundactivity")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-BACKGROUNDACTIVITY-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-BACKGROUNDACTIVITY "):
                self.note(line)

        done = "FOUNDATION-BACKGROUNDACTIVITY DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-BACKGROUNDACTIVITY DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-BACKGROUNDACTIVITY %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-BACKGROUNDACTIVITY RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-BACKGROUNDACTIVITY-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
