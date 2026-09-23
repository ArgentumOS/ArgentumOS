# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""foundation_streamtask: a duplex connection as a task, and the semantics Apple's pages state (W7, §58).

The probe is its own server (the pattern the authentication, metrics and redirect units set) and its server's
script is what gives each check its meaning: it answers, waits, answers again (so a minimum can be told from a
read that returned early), serves a longer run for the cap, goes quiet for the timeout, and answers late for
the zero-timeout leg. The unit does NOT cover the two TLS doors — §58.1 is that leg, and it is owed.
"""

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_streamtask"
CHECKS = (
    "the-write-completes",
    "and-the-far-end-read-it",
    "the-read-delivers-what-the-server-said",
    "the-minimum-is-a-minimum",
    "the-cap-is-a-cap",
    "the-timeout-is-a-cancel",
    "a-zero-timeout-does-not-fire",
    "close-write-reports-its-side",
    "and-the-read-side-too",
    "and-the-task-ends-when-both-halves-close",
    "and-the-far-end-sees-the-connection-end",
    "and-it-was-measured",
    "capture-streams-is-refused-with-its-ground",
    "the-probe-served-what-it-was-asked",
    "the-tls-fixture-is-up",
    "the-tls-handshake-completes",
    "the-request-goes-through-the-tunnel",
    "and-the-reply-comes-back-through-it",
    "and-the-far-end-answered-through-the-tunnel",
    "and-taking-the-tunnel-down-leaves-a-working-connection",
)


class Case(BaseCase):
    title = "The stream task: minimum, cap, timeout-as-a-cancel, half-close and the refused door (W7, §58)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_streamtask")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell" if ready else "no shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-STREAMTASK-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-STREAMTASK"):
                self.note(line)
        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-STREAMTASK %s ok" % c for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-STREAMTASK DONE" in out
        self.check("probe-ran", ran, "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
