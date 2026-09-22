# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""fn_receiver_probe - the local receiver, alone. Passes iff the probe SURVIVES and reaches its marker."""

from harness import BaseCase

PROBE = "/System/Shared/tests/fn_receiver_probe"
CHECKS = ()


class Case(BaseCase):
    title = "fn_receiver_probe: does netcat -l start, and does a posted body reach it?"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("fn_receiver_probe")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready else "no shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s; echo FNRCV-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FNRCV"):
                self.note(line)
        done = "FNRCV DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
