# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The Foundation's value types — F2's acceptance.

docs/design/foundation-plan.md §5 (F2). The probe is
`/System/Shared/tests/foundation_value`, built from two translation units; the
support unit imports ONLY the umbrella header, so a complete
`<foundation/Foundation.h>` is part of what is being checked.

  * `number-convert`     — every boxed scalar conversion agrees;
  * `number-value`       — the decision that matters: VALUE semantics ACROSS
                           TYPES, so `numberWithInt:1` equals
                           `numberWithDouble:1.0` AND hashes alike (equal objects
                           must hash equal), while 1 != 2 and 1 != an NSString;
  * `number-compare`     — `-compare:` returns -1/0/1;
  * `number-description` — a number renders itself through NSString;
  * `data-roundtrip`     — bytes/length, and equality by value (both a true and a
                           false case);
  * `data-mutable`       — an append is visible through the immutable interface,
                           and `-copy` of a mutable data is a SNAPSHOT;
  * `date`               — an exact epoch round-trip, ordering, `+date` reading a
                           real clock, and a self-description;
  * `cross-tu`           — values built in the other unit compare equal to local
                           ones, which is what makes them ordinary objects.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_value"
CHECKS = (
          "number-convert", "number-value", "number-compare", "number-description", "number-api-complete", "number-matrix", "data-roundtrip", "data-mutable", "date", "cross-tu", "data-api-complete", "date-api-complete", "data-extras", "data-base64", "data-mutable-extras", "date-extras", "data-block-enumeration", "data-url-url", "data-url-write", "data-url-read", "data-url-nonfile-url", "data-url-nonfile-refuse", "date-nscoding-round-trip",
          )


class Case(BaseCase):
    title = "the Foundation's value types: boxed numbers, data, dates"
    tier = "fast"
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_value")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-VALUE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-VALUE "):
                self.note(line)

        done = "FOUNDATION-VALUE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-VALUE DONE; output tail: "
                        + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-VALUE %s ok$" % re.escape(c),
                                    out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS),
                           ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-VALUE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-VALUE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
