# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSURLSession + NSURLSessionTask/NSURLSessionDataTask — W7 slice 2c's session half, row 2 (the MODEL).

docs/design/foundation-plan.md W7; docs/design/foundation-transport-plan.md §4, slice 2c. NOTHING
TRANSFERS HERE: this row is the session and its tasks as a model — identity, the configuration snapshot,
the task state machine and the enumeration. Running a task through FNCURLURLProtocol is the next row.

The probe is `/System/Shared/tests/foundation_urlsession`, ONE unit, importing only
`<Foundation/Foundation.h>`.

  * `shared-session-is-a-singleton`                 — identity, and the defaults behind it;
  * `session-snapshots-its-configuration`           — a COPY, so later edits cannot reach a running
                                                      session;
  * `session-keeps-its-delegate-and-queue`          — the three-argument door;
  * `task-starts-suspended-and-resumes`             — a new task is suspended, which is why -resume is an
                                                      explicit act;
  * `task-cancel-completes-and-records-the-error`   — the simplification ASSERTED: no transfer runs, so a
                                                      cancel goes to Completed with NSURLErrorCancelled;
  * `a-completed-task-does-not-resume`              — a task cannot be dragged back into life;
  * `task-identifiers-are-unique-and-in-order`      — from the session, in creation order;
  * `task-carries-its-request-and-description`      — the request as a value, priority 0.5, no bytes yet;
  * `task-description-round-trips`                  — a caller's own label;
  * `session-reports-its-tasks`                     — the data array is real, the other two ALWAYS empty;
  * `invalidate-and-cancel-cancels-the-tasks`       — and a new task is refused (nil), not dead;
  * `session-api-inventory`                         — owed selectors exist; the execution doors, the
                                                      download/upload/stream/websocket factories, the
                                                      challenge member and the coder doors are ABSENT.

THE TWO THAT EARN THEIR PLACE: `session-snapshots-its-configuration` is the one behaviour this row
observes rather than declares, and `task-cancel-completes-and-records-the-error` is the check that will
HAVE to change when execution lands — which is exactly why it is written down.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_urlsession"
CHECKS = ("shared-session-is-a-singleton", "session-snapshots-its-configuration",
          "session-keeps-its-delegate-and-queue", "task-starts-suspended-and-resumes",
          "task-cancel-completes-and-records-the-error", "a-completed-task-does-not-resume",
          "task-identifiers-are-unique-and-in-order", "task-carries-its-request-and-description",
          "task-description-round-trips", "session-reports-its-tasks",
          "invalidate-and-cancel-cancels-the-tasks", "session-api-inventory")


class Case(BaseCase):
    title = "NSURLSession + NSURLSessionTask: the session and task model (W7 slice 2c, session)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_urlsession")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-URLSESSION-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-URLSESSION "):
                self.note(line)

        done = "FOUNDATION-URLSESSION DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-URLSESSION DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-URLSESSION %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-URLSESSION RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-URLSESSION-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
