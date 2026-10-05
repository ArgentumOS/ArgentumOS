# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""foundation_httpcookiestorage: the store, the policy, and RFC 6265's two matching rules."""

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_httpcookiestorage"
CHECKS = (
    "the-shared-store-is-one-per-process",
    "a-store-of-ones-own-is-not-the-shared-one",
    "setting-the-same-cookie-replaces-it",
    "the-replacement-won",
    "the-store-told-its-observers",
    "a-refused-change-stays-silent",
    "the-policy-gates-insertion",
    "a-refused-set-posts-no-change",
    "and-an-accepting-store-holds-it",
    "a-host-matched-cookie-is-sent",
    "a-dot-domain-reaches-a-subdomain",
    "a-lookalike-host-is-refused",
    "a-path-prefix-is-sent",
    "a-secure-cookie-stays-on-https",
    "the-sort-is-nssortdescriptors",
    
    
    "the-group-container-store-door-is-absent",
    "the-deprecated-accept-policy-notification-is-absent",
)


class Case(BaseCase):
    title = "NSHTTPCookieStorage: the store, the policy and RFC 6265's matching rules (W7 slice 3)"
    tier = "fast"
    shared_session = True

    def run(self, ctx):
        ctx.require_guest_file("foundation_httpcookiestorage")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell" if ready else "no shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-HTTPCOOKIESTORAGE-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-HTTPCOOKIESTORAGE"):
                self.note(line)
        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-HTTPCOOKIESTORAGE %s ok" % c for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-HTTPCOOKIESTORAGE DONE" in out
        self.check("probe-ran", ran, "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
