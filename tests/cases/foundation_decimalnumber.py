# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""foundation_decimalnumber — NSDecimalNumber, its handler and the NSNumber bridge (W3b).

One unit, importing only `<Foundation/Foundation.h>`. The probe asserts the documented string grammar
INCLUDING the locale's decimal separator, the object layer's arithmetic, the one asymmetry the
documentation states in words (overflow raises, loss of precision does not), the behaviour's scale, the
four rounding modes through the object API, comparison and NaN, and the NSNumber bridge in both
directions — where the hash has to agree with an equality that crosses the class boundary.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_decimalnumber"
CHECKS = ("decimalnumber-from-string", "decimalnumber-arithmetic", "decimalnumber-default-behavior",
          "decimalnumber-overflow", "decimalnumber-scale", "decimalnumber-rounding",
          "decimalnumber-compare", "number-decimal-bridge", "decimalnumber-handler",
          )


class Case(BaseCase):
    title = "NSDecimalNumber: the object layer, its behaviours, and NSNumber's decimal bridge"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_decimalnumber")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-DECIMALNUMBER-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-DECIMALNUMBER "):
                self.note(line)

        done = "FOUNDATION-DECIMALNUMBER DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-DECIMALNUMBER DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-DECIMALNUMBER %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else ("%d of %d ok; missing: %s"
                         % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing))))

        fails = [l for l in out.splitlines() if " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-DECIMALNUMBER RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-DECIMALNUMBER-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
