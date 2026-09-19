# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSOperation / NSOperationQueue — F13.19's acceptance.

docs/design/foundation-plan.md §10, and the second-to-last name in its mechanism table. An operation
is a unit of work WITH STATE (executing, finished, cancelled, waiting for a dependency) and a queue is
what decides WHEN; the scheduler's core is that it re-examines what is READY every time an operation
FINISHES, because finishing is what makes the next one ready.

The probe is `/System/Shared/tests/foundation_operation`, ONE unit, importing only
`<foundation/Foundation.h>`.

  * `operation-subclass-runs`       — a subclass's `-main` runs, and the state follows it;
  * `operation-base-main-raises`    — THE REFUSAL THAT MUST BE LOUD: a subclass that forgot to
                                      override `-main` fails instead of succeeding at nothing;
  * `operation-dependency-order`    — an operation added FIRST but DEPENDING on another runs SECOND:
                                      the graph decides, not the addition order;
  * `queue-runs-and-drains`         — three operations run and the queue drains to empty;
  * `queue-serial-order`            — with a limit of ONE the start order IS the addition order,
                                      which is what makes the limit observable at all;
  * `queue-suspend-holds-work`      — a suspended queue runs nothing while it is suspended;
  * `queue-resume-runs`             — and runs it on resume;
  * `queue-cancel-all`              — SUSPENDED FIRST, so cancellation is deterministic: nothing had
                                      begun, so "none of them ran" is a fact rather than a race;
  * `queue-current-inside-operation` — `+currentQueue` answers the queue the operation is running in.

NAMED ABSENT, in the headers: `-completionBlock`/`-addOperationWithBlock:` (no blocks in this
library's public headers), priority and quality-of-service ordering, asynchronous operations that own
their completion, and the main queue running ON the main thread — this library's run loop has timers
and no sources, so `+mainQueue` is a serial queue over worker threads.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_operation"
CHECKS = ("operation-subclass-runs", "operation-base-main-raises", "operation-dependency-order",
          "queue-runs-and-drains", "queue-serial-order", "queue-suspend-holds-work",
          "queue-resume-runs", "queue-cancel-all", "queue-current-inside-operation")


class Case(BaseCase):
    title = "NSOperation / NSOperationQueue: the unit of work and the scheduler"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_operation")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-OPERATION-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-OPERATION "):
                self.note(line)

        done = "FOUNDATION-OPERATION DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-OPERATION DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-OPERATION %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-OPERATION RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-OPERATION-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
