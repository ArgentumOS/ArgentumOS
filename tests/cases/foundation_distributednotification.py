# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSDistributedNotificationCenter — §62.80's acceptance, and the sixth family closed.

docs/design/foundation-plan.md §62.80. One row: the class.

THE CLASS SHIPPED AND THE DECISION THAT KEPT IT OUT WAS REVERSED — the same reversal §62.23 made for
the removed post-baseline families, and for the same reason: "the door is absent rather than stubbed" is the right rule for a DOOR and
the wrong one for a CLASS whose LOCAL half is real. What is real is everything a process can do on its own:
observers each carrying a SUSPENSION BEHAVIOUR, a suspended center that drops, holds or coalesces what arrives,
and the resume that flushes it.

THE BUS IS THE BOUNDARY, stated by two doors rather than implied: `+notificationCenterForType:` answers this
process's center for `NSLocalNotificationCenterType` and refuses any other type, and
`NSDistributedNotificationPostToAllSessions` is refused for the same ground.

The probe is `/System/Shared/tests/foundation_distributednotification`, ONE unit, importing only
`<Foundation/Foundation.h>`.

  * `the-default-center-is-this-classes-own-and-the-local-type-answers-it`;
  * `an-unknown-center-type-is-refused-by-name` — the bus this system does not have, named;
  * `a-post-reaches-a-local-observer-with-its-user-info`;
  * `the-name-and-object-filters-are-honoured`;
  * `a-suspended-center-drops-for-a-drop-observer`;
  * `deliver-immediately-is-honoured-while-suspended` — via the observing record's own behaviour;
  * `a-suspended-center-coalesces-one-per-name-for-a-coalescing-observer`;
  * `the-resume-flushes-what-was-held-in-arrival-order` — Hold's promise, which is about ORDER;
  * `the-resume-delivers-a-coalesced-notification-once` — Coalesce's promise, which is about COUNT;
  * `a-posters-immediate-request-is-honoured-while-suspended` — the same promise from the poster's side;
  * `the-cross-session-option-is-refused-by-name`;
  * `an-observer-removed-while-suspended-gets-nothing-from-the-resume` — the flush asks the registry whether the
                                     observer is still there.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_distributednotification"
CHECKS = ("the-default-center-is-this-classes-own-and-the-local-type-answers-it",
          "an-unknown-center-type-is-refused-by-name",
          "a-post-reaches-a-local-observer-with-its-user-info",
          "the-name-and-object-filters-are-honoured",
          "a-suspended-center-drops-for-a-drop-observer",
          "deliver-immediately-is-honoured-while-suspended",
          "a-suspended-center-coalesces-one-per-name-for-a-coalescing-observer",
          "the-resume-flushes-what-was-held-in-arrival-order",
          "the-resume-delivers-a-coalesced-notification-once",
          "a-posters-immediate-request-is-honoured-while-suspended",
          "the-cross-session-option-is-refused-by-name",
          "an-observer-removed-while-suspended-gets-nothing-from-the-resume")


class Case(BaseCase):
    title = "NSDistributedNotificationCenter: the local half is real, the bus is not here"
    tier = "fast"
    # No fixture and no file system work: it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_distributednotification")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-DISTRIBUTEDNOTIFICATION-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-DISTRIBUTEDNOTIFICATION "):
                self.note(line)

        done = "FOUNDATION-DISTRIBUTEDNOTIFICATION DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-DISTRIBUTEDNOTIFICATION DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-DISTRIBUTEDNOTIFICATION %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-DISTRIBUTEDNOTIFICATION RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-DISTRIBUTEDNOTIFICATION-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
