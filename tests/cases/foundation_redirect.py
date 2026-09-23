# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""foundation_redirect: following a redirect, decided by the delegate, bounded by the hop limit (W7, §54).

The probe is its own HTTP server (the pattern foundation_authloop and foundation_metricsdelivery set) and
drives five transfers: a 302 that is followed, a moved POST, a 307 that must keep its method, a redirect the
delegate declines, and a chain that never ends. The rules are checked at the FAR END (what the second request
looks like on the wire) and in the RECORD the delegate was handed (transactions in order, redirect count).
"""

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_redirect"
CHECKS = (
    "the-followed-task-answers-with-the-final-body",
    "and-the-second-request-is-the-one-that-arrived",
    "the-task-remembers-both-requests",
    "the-record-holds-the-chain",
    "a-moved-post-arrives-as-a-get",
    "a-307-keeps-its-method",
    "a-declined-redirect-is-not-a-failure",
    "and-nothing-was-run-in-its-place",
    "and-the-delegate-was-asked",
    "and-the-declined-hop-was-not-counted",
    "a-redirect-loop-ends",
    "at-the-bound-and-not-before",
    "and-the-failure-names-itself",
    "the-probe-served-what-it-was-asked",
    "and-every-task-ended",
)


class Case(BaseCase):
    title = "Following a redirect: the door, the method rules, the refusal and the hop bound (W7, §54)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_redirect")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell" if ready else "no shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-REDIRECT-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-REDIRECT"):
                self.note(line)
        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-REDIRECT %s ok" % c for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-REDIRECT DONE" in out
        self.check("probe-ran", ran, "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
