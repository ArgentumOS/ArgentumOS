# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""foundation_cachehooks: the bridge's cache hooks, proved by a contact count (W7 slice 5)."""

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_cachehooks"
CHECKS = (
    "the-probe-installs-its-own-shared-cache",
    "the-probe-binds-its-own-listener",
    "the-first-request-reaches-the-server",
    "a-hit-is-answered-from-the-cache",
    "and-the-server-was-not-contacted-again",
    "the-cached-body-is-what-was-served",
    "the-bridge-did-not-reach-the-listener",
    "a-no-store-response-is-not-kept",
    "and-it-goes-out-again-next-time",
    "the-cache-door-is-asked",
    "a-nil-answer-keeps-nothing",
    "the-refused-entry-goes-out-again",
)


class Case(BaseCase):
    title = "The cache hooks: a hit without a connection, and no-store honoured (W7 slice 5)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_cachehooks")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell" if ready else "no shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-CACHEHOOKS-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-CACHEHOOKS"):
                self.note(line)
        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-CACHEHOOKS %s ok" % c for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-CACHEHOOKS DONE" in out
        self.check("probe-ran", ran, "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
