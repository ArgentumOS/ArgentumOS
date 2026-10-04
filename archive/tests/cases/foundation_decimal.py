# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""foundation_decimal — the NSDecimal C surface (W3).

One unit, importing only `<Foundation/Foundation.h>`. The probe asserts Apple's four rounding modes over
the table its documentation gives, the arithmetic that makes a decimal worth having (0.1 + 0.2 is 0.3),
division both exact and inexact with its LOSS OF PRECISION reported, comparison across two
representations of one value, NaN propagation, the printed form, the documented aliasing, and the
model's own overflow and underflow.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_decimal"
CHECKS = ("decimal-round-modes", "decimal-arithmetic", "decimal-division", "decimal-compare",
          "decimal-strings", "decimal-alias-power", "decimal-limits-shape", "decimal-limits",
          )


class Case(BaseCase):
    title = "NSDecimal: base-10 arithmetic, its four rounding modes, and its error codes"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_decimal")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-DECIMAL-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-DECIMAL "):
                self.note(line)

        done = "FOUNDATION-DECIMAL DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-DECIMAL DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-DECIMAL %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else ("%d of %d ok; missing: %s"
                         % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing))))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-DECIMAL RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-DECIMAL-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
