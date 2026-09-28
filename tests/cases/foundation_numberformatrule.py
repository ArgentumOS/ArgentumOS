# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSLocalizedNumberFormatRule — §62.81's acceptance.

docs/design/foundation-plan.md §62.81. One row: the class.

THE PUBLISHED SURFACE IS ONE DOOR — Apple's page documents `+automatic` and the two conformances and nothing else
— so this header is COMPLETE against what Apple publishes, and the boundary is the one §62.79's rules state a
family over: NOTHING HERE FORMATS A NUMBER. A rule is a thing a formatter would consult; this system's formatters
do their own work, so the value exists, answers for itself, and drives nothing.

The probe is `/System/Shared/tests/foundation_numberformatrule`, ONE unit, importing only
`<Foundation/Foundation.h>`.

  * `the-automatic-rule-is-a-value-and-fresh-each-call`;
  * `two-automatic-rules-are-equal-and-hash-alike`;
  * `the-rule-is-not-equal-to-anything-else` — and THE NIL ARM IS DELIBERATELY ABSENT: `-isEqual:` is declared
                                     with a non-null argument, so passing nil is a `-Wnonnull` warning, and this
                                     project holds its warning count at zero;
  * `the-rule-copies-as-a-distinct-equal-value`;
  * `the-rule-round-trips-through-a-keyed-archiver`;
  * `secure-coding-is-claimed`.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_numberformatrule"
CHECKS = ("the-automatic-rule-is-a-value-and-fresh-each-call",
          "two-automatic-rules-are-equal-and-hash-alike",
          "the-rule-is-not-equal-to-anything-else",
          "the-rule-copies-as-a-distinct-equal-value",
          "the-rule-round-trips-through-a-keyed-archiver",
          "secure-coding-is-claimed")


class Case(BaseCase):
    title = "NSLocalizedNumberFormatRule: the localization's number-format rule as a value"
    tier = "fast"
    # No fixture and no file system work: it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_numberformatrule")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-NUMBERFORMATRULE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-NUMBERFORMATRULE "):
                self.note(line)

        done = "FOUNDATION-NUMBERFORMATRULE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-NUMBERFORMATRULE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-NUMBERFORMATRULE %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-NUMBERFORMATRULE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-NUMBERFORMATRULE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
