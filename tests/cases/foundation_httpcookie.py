# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""foundation_httpcookie: the cookie as a value, driven through both wire conversions."""

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_httpcookie"
CHECKS = (
    "a-cookie-without-properties-is-refused",
    "a-cookie-without-a-name-is-refused",
    "a-cookie-without-a-value-is-refused",
    "the-required-pair-is-enough",
    "the-path-defaults-to-the-rfc-one",
    "a-cookie-with-no-domain-is-host-only",
    "no-expiry-means-session-only",
    "a-response-field-becomes-a-cookie",
    "the-attribute-case-is-ignored",
    "the-two-boolean-attributes-arrive",
    "an-absolute-expiry-is-understood",
    "the-domain-comes-from-the-url-when-absent",
    "cookies-become-a-request-field",
    "the-property-dictionary-round-trips",
    "the-value-is-not-the-wire-format",
    "a-property-with-no-wire-attribute-survives-the-dictionary",
    "and-it-does-not-leak-into-the-request-field",
)


class Case(BaseCase):
    title = "NSHTTPCookie: the value type and its two wire conversions (W7 slice 3)"
    tier = "fast"
    shared_session = True

    def run(self, ctx):
        ctx.require_guest_file("foundation_httpcookie")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell" if ready else "no shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-HTTPCOOKIE-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-HTTPCOOKIE"):
                self.note(line)
        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-HTTPCOOKIE %s ok" % c for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-HTTPCOOKIE DONE" in out
        self.check("probe-ran", ran, "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
