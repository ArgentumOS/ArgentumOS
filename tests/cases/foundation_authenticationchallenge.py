# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""foundation_authenticationchallenge: the challenge's mechanics (W7 slice 4)."""

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_authenticationchallenge"
CHECKS = (
    "the-challenge-carries-its-space",
    "a-first-attempt-is-counted-zero",
    "a-first-attempt-has-no-proposed-credential",
    "a-first-attempt-has-no-error",
    "a-retry-carries-what-was-proposed",
    "and-how-many-times-it-has-failed",
    "the-copy-door-keeps-the-space",
    "the-copy-door-keeps-the-count",
    "the-copy-door-keeps-the-credential",
    "the-documented-sender-argument-still-compiles",
    "but-the-sender-accessor-is-absent",
)


class Case(BaseCase):
    title = "NSURLAuthenticationChallenge: the mechanics (W7 slice 4)"
    tier = "fast"
    shared_session = True

    def run(self, ctx):
        ctx.require_guest_file("foundation_authenticationchallenge")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell" if ready else "no shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-AUTHENTICATIONCHALLENGE-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-AUTHENTICATIONCHALLENGE"):
                self.note(line)
        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-AUTHENTICATIONCHALLENGE %s ok" % c
                              for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-AUTHENTICATIONCHALLENGE DONE" in out
        self.check("probe-ran", ran, "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
