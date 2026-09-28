# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The five value-transformer names Apple registers — §62.96's acceptance.

THE CHECK THAT SEPARATES THE TWO WAYS OF BEING FINDABLE: this library's registry resolves an unknown name by
looking for a CLASS of that name (the trick the one transformer shipped before this unit uses), which would make
these five reachable while leaving `+valueTransformerNames` EMPTY. So the first check asks the documented list,
and the rest ask the transformers by value, on both sides of every question: the nil pair both ways, the negator
both ways, and both unarchivers round-tripping a real archiver's output.

THE NEGATOR'S DOMAIN IS A BOUNDARY AND IT IS ASSERTED: "NegateBoolean" promises a Boolean, so a string answers
nil — the base class's own contract for what cannot be transformed rather than a coercion.

AND THE NAME STRINGS ARE PINNED, because they are this library's choice (D2): a nib or a model spells @"NSIsNil"
as a literal, so a value nobody publishes is asserted here instead of drifting. One of the five is
`NSKeyedUnarchiveFromDataTransformerName`, the deprecated row the ledger owed — the header that dismissed it as
"struck on the surface ledger" was wrong twice, and both corrections are recorded there.

The probe is `/System/Shared/tests/foundation_transformers`, ONE unit, importing only `<Foundation/Foundation.h>`.

THE `CHECKS` TUPLE IS DERIVED FROM THE PROBE'S OWN NAMES, NOT TYPED (§62.86's rule).
"""
import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_transformers"
CHECKS = (
    "the-five-names-are-in-the-registry",
    "the-is-nil-pair-answers-about-nil",
    "the-negator-negates-a-boolean-and-refuses-what-is-not-one",
    "the-two-unarchivers-round-trip-their-own-archive",
    "the-name-strings-are-the-transformer-names",
)


class Case(BaseCase):
    title = "NSValueTransformer: the five registered names Apple declares (§62.96)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_transformers")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-TRANSFORMERS-STATUS-ECHO=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-TRANSFORMERS "):
                self.note(line)

        done = "FOUNDATION-TRANSFORMERS DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-TRANSFORMERS DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-TRANSFORMERS %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-TRANSFORMERS RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-TRANSFORMERS-STATUS-ECHO=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
