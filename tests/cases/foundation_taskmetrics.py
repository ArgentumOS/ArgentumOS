# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""foundation_taskmetrics: the metrics records and the two answers this system gives differently."""

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_taskmetrics"
CHECKS = (
    "a-fresh-record-has-no-dates",
    "a-fresh-record-counts-nothing",
    "the-session-fills-the-instants",
    "the-bytes-are-counted",
    "the-transaction-characteristics-are-read",
    "the-boolean-getters-keep-their-is-spelling",
    "a-plain-boolean-stays-plain",
    "the-secure-connection-start-stays-nil",
    "the-task-record-carries-its-transactions",
    "and-its-own-span",
    "and-how-many-redirects",
    "the-deprecated-fetch-type-is-absent",
    "the-known-fetch-types-are-present",
    "the-resolution-protocols-are-all-declared",
)


class Case(BaseCase):
    title = "NSURLSessionTaskMetrics: the records, and the documented absences (W7 slice 6)"
    tier = "fast"
    shared_session = True

    def run(self, ctx):
        ctx.require_guest_file("foundation_taskmetrics")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell" if ready else "no shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-TASKMETRICS-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-TASKMETRICS"):
                self.note(line)
        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-TASKMETRICS %s ok" % c for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-TASKMETRICS DONE" in out
        self.check("probe-ran", ran, "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
