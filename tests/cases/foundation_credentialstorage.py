# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""foundation_credentialstorage: the store keyed by a protection space (W7 slice 4)."""

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_credentialstorage"
CHECKS = (
    "the-shared-store-is-one-per-process",
    "a-credential-is-filed-under-its-space",
    "a-rebuilt-space-finds-the-entry",
    "two-credentials-coexist-for-one-realm",
    "setting-the-same-credential-twice-does-not-duplicate",
    "and-a-repeat-is-still-a-change",
    "no-default-means-none",
    "the-default-is-kept",
    "removing-what-is-not-there-is-silent",
    "removal-takes-the-credential-out",
    "all-credentials-answers-by-space",
    "the-store-announced-its-changes",
    
    "the-removal-options-door-is-absent",
    "and-so-is-its-task-form",
)


class Case(BaseCase):
    title = "NSURLCredentialStorage: the store keyed by protection space (W7 slice 4)"
    tier = "fast"
    shared_session = True

    def run(self, ctx):
        ctx.require_guest_file("foundation_credentialstorage")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell" if ready else "no shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-CREDENTIALSTORAGE-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-CREDENTIALSTORAGE"):
                self.note(line)
        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-CREDENTIALSTORAGE %s ok" % c for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-CREDENTIALSTORAGE DONE" in out
        self.check("probe-ran", ran, "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
