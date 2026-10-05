# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The two JSON WRITING options — §62.94's acceptance, which closes the JSON family.

BOTH WERE SMALLER THAN THE NOTE THAT DEFERRED THEM, and each for a reason the probe measures rather
than argues. `withoutEscapingSlashes` had been called "a change to the default output rather than an
addition" — but the writer has answered `\\/` for a slash since it was written (Apple's own default), so
the flag only turns an escape OFF and a caller who does not pass it sees byte-identical output; the
note had been written from the option's NAME rather than from this file's behaviour. And
`writingFragmentsAllowed` is the second question `+dataWithJSONObject:options:error:` asks, not a change
to `+isValidJSONObject:` — that door keeps answering NO for a bare scalar, which is Apple's rule for it.

THE ESCAPE IS ASSERTED BY TEXT ON BOTH SIDES (`http:\\/\\/a\\/b` without the flag, `http://a/b` with it)
AND BY ROUND TRIP, because an escape a reader cannot undo would make the default wrong rather than
merely different. A THIRD CHECK KEEPS "unescaped slashes" FROM MEANING "no escaping": a string carrying
a quote, a backslash, a tab, a newline and a control character still escapes every one of them.

AND THE FLAG DOES NOT BLESS AN INVALID OBJECT: a date and a NaN are still refused with it set, because
the option opens the TOP LEVEL's question and not the nested rules'. The two flags are also exercised
TOGETHER, which is the one case the reading side's `json5Allowed` could not express.

The probe is `/System/Shared/tests/foundation_jsonwrite`, ONE unit, importing only `<Foundation/Foundation.h>`.

THE `CHECKS` TUPLE IS DERIVED FROM THE PROBE'S OWN NAMES, NOT TYPED (§62.86's rule).
"""
import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_jsonwrite"
CHECKS = (
    "the-default-escapes-a-slash",
    "the-option-stops-escaping-slashes",
    "the-option-changes-nothing-else",
    "a-top-level-scalar-is-refused-by-default",
    "the-fragment-option-writes-a-scalar",
    "the-fragment-option-does-not-bless-an-invalid-object",
    "the-writing-bit-values-are-apples",
    "the-two-flags-combine",
)


class Case(BaseCase):
    title = "NSJSONSerialization: the writing options, and the JSON family closed (§62.94)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_jsonwrite")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-JSONWRITE-STATUS-ECHO=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-JSONWRITE "):
                self.note(line)

        done = "FOUNDATION-JSONWRITE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-JSONWRITE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-JSONWRITE %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-JSONWRITE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-JSONWRITE-STATUS-ECHO=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
