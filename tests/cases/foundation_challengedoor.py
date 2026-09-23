# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""foundation_challengedoor: the two authentication-challenge delegate doors (W7 slice 4)."""

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_challengedoor"
CHECKS = (
    "the-task-door-is-asked-first",
    "and-its-answer-comes-back",
    "with-the-credential-it-handed-over",
    "the-session-door-is-the-fallback",
    "no-door-means-default-without-waiting",
    "the-disposition-values-are-ours-and-ordered",
)


class Case(BaseCase):
    title = "The challenge delegate doors: which is asked, and what happens with none (W7 slice 4)"
    tier = "fast"
    shared_session = True

    def run(self, ctx):
        ctx.require_guest_file("foundation_challengedoor")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell" if ready else "no shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-CHALLENGEDOOR-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-CHALLENGEDOOR"):
                self.note(line)
        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-CHALLENGEDOOR %s ok" % c for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-CHALLENGEDOOR DONE" in out
        self.check("probe-ran", ran, "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
