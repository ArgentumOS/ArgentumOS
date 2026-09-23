# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""foundation_urlcredential: the credential as a value, and the secret it must not leak."""

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_urlcredential"
CHECKS = (
    "the-factory-keeps-what-it-was-given",
    "the-persistence-is-kept",
    "has-password-answers-yes",
    "the-none-persistence-is-zero",
    "the-description-does-not-print-the-password",
    "the-description-says-a-password-is-set",
    "the-description-does-show-the-user",
    "a-copy-carries-the-same-secret",
    "the-trust-initialiser-is-absent",
    "the-identity-initialiser-is-absent",
    "the-trust-factory-is-absent",
    "the-identity-properties-are-absent",
    "the-synchronizable-persistence-is-absent",
)


class Case(BaseCase):
    title = "NSURLCredential: the credential as a value (W7 slice 4)"
    tier = "fast"
    shared_session = True

    def run(self, ctx):
        ctx.require_guest_file("foundation_urlcredential")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell" if ready else "no shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-URLCREDENTIAL-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-URLCREDENTIAL"):
                self.note(line)
        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-URLCREDENTIAL %s ok" % c for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-URLCREDENTIAL DONE" in out
        self.check("probe-ran", ran, "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
