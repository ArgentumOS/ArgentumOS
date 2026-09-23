# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""foundation_metricsdelivery: what a transfer cost, arriving where Apple says (W7 slice 6, §52).

The probe is its own HTTP server — the pattern foundation_authloop established, because this guest cannot
listen with a receiver tool. One real transfer runs through the curl bridge and the DELEGATE is asked what
it was handed: the fields that have a source, the order of the two ending calls, and the absences that are
documented rather than filled with something plausible.
"""

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_metricsdelivery"
CHECKS = (
    "the-probe-binds-its-own-listener",
    "the-transfer-reaches-the-probe",
    "and-asks-for-the-path-it-was-given",
    "the-task-ends-with-the-answer",
    "the-metrics-door-is-delivered",
    "the-ending-was-told-once-and-the-metrics-once",
    "and-the-metrics-came-first",
    "the-task-carries-one-transaction",
    "and-it-names-the-exchange",
    "it-is-a-network-load-and-names-the-address",
    "the-instants-are-ordered",
    "the-bytes-are-counted",
    "the-task-span-covers-its-transaction",
    "the-documented-absences-hold",
)


class Case(BaseCase):
    title = "The metrics delivery: a real transfer's record, delivered before the ending (W7 slice 6, §52)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_metricsdelivery")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell" if ready else "no shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-METRICSDELIVERY-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-METRICSDELIVERY"):
                self.note(line)
        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-METRICSDELIVERY %s ok" % c for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-METRICSDELIVERY DONE" in out
        self.check("probe-ran", ran, "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
