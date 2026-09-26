# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""foundation_authloop: the whole 401 path, with the probe as its own server (W7 slice 4)."""

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_authloop"
CHECKS = (
    "the-probe-binds-its-own-listener",
    "the-transfer-connects",
    "the-first-request-came-without-credentials",
    "the-transfer-comes-back-after-the-401",
    "the-delegate-was-asked-exactly-once",
    "the-challenge-carried-a-sender",
    "the-second-request-carries-the-credential",
    "and-not-in-plaintext",
    "the-loop-terminates",
    "the-attempt-guard-held",
    "the-reissue-is-two-transactions",
)


class Case(BaseCase):
    title = "The authentication loop: 401, the door, the re-issue, and the credential on the wire (W7 slice 4)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_authloop")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell" if ready else "no shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-AUTHLOOP-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-AUTHLOOP"):
                self.note(line)
        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-AUTHLOOP %s ok" % c for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-AUTHLOOP DONE" in out
        self.check("probe-ran", ran, "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
