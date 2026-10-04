# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""foundation_urlcache: the cache, its key rule and its storage policy (W7 slice 5)."""

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_urlcache"
CHECKS = (
    "the-shared-cache-is-one-per-process",
    "and-a-cache-of-ones-own-is-not-it",
    "a-stored-response-is-found",
    "a-rebuilt-request-finds-it",
    "a-different-url-does-not",
    "a-different-method-does-not",
    "a-not-allowed-response-is-refused",
    "and-it-does-not-count-toward-the-usage",
    "the-usage-is-the-bytes-it-holds",
    "an-in-memory-only-response-is-kept",
    "removal-takes-one-out",
    "remove-all-empties-it",
    "a-response-cached-before-the-date-goes",
    "the-disk-is-not-written-to",
    
    "the-shared-cache-can-be-replaced",
)


class Case(BaseCase):
    title = "NSURLCache: the key rule and the storage policy (W7 slice 5)"
    tier = "fast"
    shared_session = True

    def run(self, ctx):
        ctx.require_guest_file("foundation_urlcache")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell" if ready else "no shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-URLCACHE-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-URLCACHE"):
                self.note(line)
        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-URLCACHE %s ok" % c for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-URLCACHE DONE" in out
        self.check("probe-ran", ran, "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
