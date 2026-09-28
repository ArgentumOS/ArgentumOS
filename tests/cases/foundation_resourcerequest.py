# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSBundleResourceRequest — §62.88's acceptance.

This closes `App Support / On-Demand Resources`: the request, its urgent-priority constant and its low-disk-space
notice.

THE ANSWER IS SHORT BECAUSE THE SYSTEM IS SHORT: there are no on-demand resources here, so a bundle's resources
are simply present or absent and there is nothing to download. That makes the doors' answers FACTS rather than
stubs — the conditional door answers YES, the begin door answers nil, and the progress object is COMPLETE — and
the probe asserts them as facts, including that the completion handlers run BEFORE THE DOORS RETURN.

WHAT IS NOT CLAIMED matters as much: the class does not pretend to know which resources a tag names (there is no
tag manifest in this system's bundles), so the ONE tag rule enforced is the documented one — an EMPTY tag set is
invalid and is reported through `NSBundleOnDemandResourceInvalidTagError`, an error code this library already
declares.

The probe is `/System/Shared/tests/foundation_resourcerequest`, ONE unit, importing `<Foundation/Foundation.h>`
plus the internal seam `<Foundation/FNBundleResourceRequest.h>`, which posts the low-disk-space notice so an
app's registration and recovery path is exercisable rather than a name nobody can fire.

THE `CHECKS` TUPLE IS DERIVED FROM THE PROBE'S OWN NAMES, NOT TYPED (§62.86's rule).
"""
import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_resourcerequest"
CHECKS = (
    "the-class-and-its-two-names-are-declared",
    "a-request-keeps-the-tags-and-the-bundle-it-was-given",
    "the-tags-are-a-copy-not-a-view",
    "the-convenience-initialiser-loads-into-the-main-bundle",
    "a-nil-tag-set-or-bundle-is-a-programming-error",
    "the-conditional-door-answers-yes-before-it-returns",
    "the-begin-door-grants-access-before-it-returns",
    "the-access-doors-tolerate-a-missing-handler",
    "ending-access-and-then-asking-again-is-safe",
    "an-empty-tag-set-is-not-available-conditionally",
    "an-empty-tag-set-is-reported-through-the-documented-error",
    "the-progress-is-complete-and-the-same-object-each-time",
    "the-priority-defaults-to-the-middle-of-apples-range-and-is-settable",
    "the-priority-is-a-hint-and-is-stored-as-given",
    "the-low-disk-space-notice-is-delivered-and-nameable",
)


class Case(BaseCase):
    title = "NSBundleResourceRequest: on-demand resources in a system without them (§62.88)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_resourcerequest")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-RESOURCEREQUEST-STATUS-ECHO=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-RESOURCEREQUEST "):
                self.note(line)

        done = "FOUNDATION-RESOURCEREQUEST DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-RESOURCEREQUEST DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-RESOURCEREQUEST %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-RESOURCEREQUEST RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-RESOURCEREQUEST-STATUS-ECHO=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
