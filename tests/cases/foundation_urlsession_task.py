# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""A session task that RUNS — W7 slice 2c's session half, row 3's first half: the execution, through the
COMPLETION-HANDLER path.

docs/design/foundation-plan.md W7; docs/design/foundation-transport-plan.md §4, slice 2c. `-resume` on a
task that has a session now asks the session to run it: the session picks a protocol class (the
configuration's own `protocolClasses` first, then slice 2a's registry), starts it, and reports the ending
into the task's state and its completion handler. The DELEGATE callbacks are the other half of this row and
land with the task/data delegate protocols.

The probe is `/System/Shared/tests/foundation_urlsession_task`, ONE unit, importing only
`<Foundation/Foundation.h>`. Its fetches are `file://` URLs, as the bridge's probe's are.

  * `completion-handler-factory-answers-a-task`          — the same task, told where its ending goes;
  * `task-resume-runs-the-transfer`                      — the handler fires and the bytes are the file's;
  * `completion-handler-receives-response-and-data`      — the response and no error;
  * `task-state-becomes-completed-and-counts-bytes`      — the task's own state, response and count;
  * `data-task-url-form-runs`                            — the URL convenience form;
  * `task-with-no-protocol-class-fails-rather-than-hanging` — NSURLErrorUnsupportedURL (-1002), and the
                                                          handler IS called;
  * `cancel-before-resume-never-starts`                  — a suspended task cancelled ends, and the
                                                          transfer it never started does not run.

`task-with-no-protocol-class-fails-rather-than-hanging` IS THE POINT: a seam that answers "no protocol
handles this" by never reporting is the worst failure mode a loading system can have, and it is exactly
what a happy path cannot see.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_urlsession_task"
CHECKS = ("completion-handler-factory-answers-a-task", "task-resume-runs-the-transfer",
          "completion-handler-receives-response-and-data",
          "task-state-becomes-completed-and-counts-bytes", "data-task-url-form-runs",
          "task-with-no-protocol-class-fails-rather-than-hanging",
          "cancel-before-resume-never-starts",
          "delegate-receives-the-body", "delegate-receives-the-ending",
          "delegate-echoes-the-session-and-its-task",
          "delegate-callbacks-arrive-on-the-delegate-queue",
          "download-handler-receives-a-location", "download-writes-the-body-where-it-says",
          "disposition-cancel-withholds-the-body", "disposition-allow-lets-the-body-through")


class Case(BaseCase):
    title = "NSURLSession task execution: the completion-handler path (W7 slice 2c, session)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it writes one fixture in the temporary directory,
    # fetches it, and reads the probe's output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_urlsession_task")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-URLSESSION-TASK-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-URLSESSION-TASK "):
                self.note(line)

        done = "FOUNDATION-URLSESSION-TASK DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-URLSESSION-TASK DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-URLSESSION-TASK %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-URLSESSION-TASK RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-URLSESSION-TASK-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
