# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""foundation_urlerror: the NSURLError names, their values, and the shape of the family (W7, §56).

Two of the checks are end to end — the error a cancelled task carries and the one an unclaimable request
carries, both read off a real task — and the rest assert the values the library depends on, the blocks each
run of codes lives in, distinctness over the whole family, and the two D2 reason enums.
"""

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_urlerror"
CHECKS = (
    "the-four-codes-the-library-reports",
    "the-domain-is-the-one-the-library-reports",
    "and-an-unclaimable-request-carries-the-unsupported-url-code",
    "the-exchange-block-is-where-it-says",
    "the-file-block-is-where-it-says",
    "the-tls-block-is-where-it-says",
    "the-body-file-block-is-where-it-says",
    "the-outliers-are-where-they-say",
    "and-no-two-codes-are-the-same",
    "the-reason-enums-are-ours-and-ordered",
    "the-keys-are-distinct-strings",
)


class Case(BaseCase):
    title = "The URL error names: the codes, the blocks they live in and the domain (W7, §56)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_urlerror")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell" if ready else "no shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-URLERROR-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-URLERROR"):
                self.note(line)
        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-URLERROR %s ok" % c for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-URLERROR DONE" in out
        self.check("probe-ran", ran, "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
