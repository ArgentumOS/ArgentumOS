# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""fn_block_mrc - the MRC twin of the crashing call (W7 slice 2c row 3 diagnostics).

Same library door, same __block-object capture, block constructed by MRC code. This case passes iff the
probe SURVIVES, so it separates "ARC's block construction" from "the MRC library's copy of a byref block".
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/fn_block_mrc"
CHECKS = ()


class Case(BaseCase):
    title = "fn_block_mrc: does an MRC-built block survive the same session door?"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("fn_block_mrc")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready else "no shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s; echo FNBLOCK-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FNBLOCK"):
                self.note(line)
        done = "FNBLOCK DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker (it SURVIVED)" if done
                   else "the probe did not reach its end marker; tail: " + out.strip()[-400:])
        self.check("exit-status", "FNBLOCK-STATUS=0" in out,
                   "the probe exited 0 (139 would be the SIGSEGV)")
