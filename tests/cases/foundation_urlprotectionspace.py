# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""foundation_urlprotectionspace: the realm as a value (W7 slice 4)."""

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_urlprotectionspace"
CHECKS = (
    "a-server-space-keeps-what-it-was-given",
    "a-server-space-is-not-a-proxy",
    "a-proxy-space-says-so",
    "a-proxy-only-field-does-not-leak",
    "an-unknown-port-is-minus-one",
    "a-method-less-space-answers-the-default",
    "https-receives-credentials-securely",
    "http-does-not",
    "ftp-does-not-either",
    "two-spaces-for-one-realm-are-equal",
    "and-they-hash-alike",
    "a-different-realm-is-a-different-space",
    "the-store-finds-it-by-a-rebuilt-key",
    "the-trust-door-is-absent",
    "the-certificate-fields-are-absent",
)


class Case(BaseCase):
    title = "NSURLProtectionSpace: the realm as a value (W7 slice 4)"
    tier = "fast"
    shared_session = True

    def run(self, ctx):
        ctx.require_guest_file("foundation_urlprotectionspace")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell" if ready else "no shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-URLPROTECTIONSPACE-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-URLPROTECTIONSPACE"):
                self.note(line)
        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-URLPROTECTIONSPACE %s ok" % c for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-URLPROTECTIONSPACE DONE" in out
        self.check("probe-ran", ran, "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
