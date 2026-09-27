# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""Ordered collection differences — §62.59's acceptance (W13's last row).

THE PROBE IS THE CONTRACT: applying a difference to the SOURCE produces the DESTINATION, which is the one thing
Apple specifies. The checks are that round trip plus the facts Apple's own pages state — the partition into
`-insertions`/`-removals`, the two index spaces (an insertion's index is in the receiver, a removal's in the
argument), the move pairing in `-associatedIndex`, the two object-suppression options, the equivalence-test
door's unset associations, and the exception for a one-sided association.

The probe is `/System/Shared/tests/foundation_difference`, ONE unit, importing only
`<Foundation/Foundation.h>`. NO FIXTURE: the collections are literals.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_difference"
CHECKS = ("a-change-carries-what-it-was-built-with",
          "a-paired-change-keeps-its-associated-index",
          "a-difference-inserts-into-the-array-it-was-taken-from",
          "an-insertion-index-is-in-the-receiver",
          "applying-a-difference-produces-the-receiver",
          "a-removal-index-is-in-the-argument",
          "a-difference-can-only-remove",
          "a-move-is-inferred-as-a-pair",
          "the-two-halves-of-a-move-name-each-other",
          "an-inferred-move-still-applies",
          "an-unsuppressed-change-carries-its-object",
          "omitting-a-side-leaves-that-side's-object-nil",
          "an-equivalence-block-decides-what-counts-as-equal",
          "the-equivalence-form-does-not-infer-moves",
          "inverting-a-difference-undoes-it",
          "transforming-maps-every-member",
          "the-index-set-form-builds-an-applicable-difference",
          "a-broken-association-raises",
          "fast-enumeration-visits-every-change",
          "a-returned-list-is-ascending-by-index",
          "an-ordered-set-difference-round-trips",
          "a-forty-element-change-is-applied-exactly")


class Case(BaseCase):
    title = "Ordered collection differences: the diff, its two index spaces, and its round trip"
    tier = "fast"
    # No fixture and no file system work: it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_difference")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-DIFFERENCE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            # the MEASUREMENTS are the point of this run: surface them, because the assertions are written from
            # these numbers rather than from a guess.
            if line.startswith("FOUNDATION-DIFFERENCE "):
                self.note(line)

        done = "FOUNDATION-DIFFERENCE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-DIFFERENCE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-DIFFERENCE %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-DIFFERENCE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-DIFFERENCE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
