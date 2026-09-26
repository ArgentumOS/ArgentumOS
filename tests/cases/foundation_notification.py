# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""foundation_notification — the notifications family (W4).

One unit, importing only `<Foundation/Foundation.h>`, so it is also the check that the umbrella still
carries the family. The probe asserts what the centre does *and* what it refuses: the delivery it makes,
the filters that stop it, both removal doors, the block form with and without a queue, and that an
observer which is deallocated WITHOUT removing itself is skipped rather than messaged.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_notification"
CHECKS = ("notification-value", "center-selector", "center-filters", "center-remove",
          "center-block-form", "center-block-queue", "center-dead-observer",
          "notification-names-are-their-own-names",
          "distributed-notification-vocabulary",
          "notification-queue-vocabulary")


class Case(BaseCase):
    title = "NSNotification / NSNotificationCenter: a value, a registry, and a filter pair"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_notification")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-NOTIFICATION-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-NOTIFICATION "):
                self.note(line)

        done = "FOUNDATION-NOTIFICATION DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-NOTIFICATION DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-NOTIFICATION %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else ("%d of %d ok; missing: %s"
                         % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing))))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-NOTIFICATION RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-NOTIFICATION-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
