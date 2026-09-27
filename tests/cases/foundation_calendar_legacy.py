# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The pre-10.9 calendar unit names — §62.46's acceptance.

Apple deprecated SIXTEEN `NSCalendarUnit` names, the wrap option and the undefined sentinel at 10.9, and §62.24's
policy put them back so a program written before then compiles and means the same thing.

THE PROPERTY THAT MATTERS IS NOT THAT THE NAMES EXIST. A restored name given a bit of its own would compile and
compute a DIFFERENT DATE — the worst kind of success, because nothing about the call site looks wrong. So the probe
compares every legacy spelling against the modern unit it names THROUGH THE CALENDAR (the same fields come out of
`-components:fromDate:` for both spellings), and prints the fields it saw.

Two of the pair's rows are NOT declared, and the ground is named: `NSCalendarUnitIsLeapMonth` and
`NSCalendarUnitIsRepeatedDay` are recorded open and **not** deprecated — they name a search over a table of
candidate dates, which this rule-based calendar does not have, so they belong to whatever unit eventually owns
that search rather than to this restore.

The probe is `/System/Shared/tests/foundation_calendar_legacy`, ONE unit, importing only `<Foundation/Foundation.h>`.

  * `the-sixteen-legacy-names-are-the-modern-units`  — the pairing, value by value;
  * `a-legacy-spelling-asks-for-the-same-fields`     — the same date fields through both spellings;
  * `the-legacy-week-name-is-the-week-of-the-year`   — the one pairing that could have been made two ways, measured;
  * `a-unit-asked-for-alone-answers-only-itself`     — the NEGATIVE CONTROL: the instrument tells units apart;
  * `the-legacy-sentinel-is-the-modern-one`          — a component nobody set answers it;
  * `the-legacy-wrap-option-is-the-modern-one`       — one option, three spellings in this tree.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_calendar_legacy"
CHECKS = ("the-sixteen-legacy-names-are-the-modern-units",
          "a-legacy-spelling-asks-for-the-same-fields",
          "the-legacy-week-name-is-the-week-of-the-year",
          "a-unit-asked-for-alone-answers-only-itself",
          "the-legacy-sentinel-is-the-modern-one",
          "the-legacy-wrap-option-is-the-modern-one")


class Case(BaseCase):
    title = "The pre-10.9 calendar unit names mean the modern units (§62.46)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_calendar_legacy")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-CALENDAR-LEGACY-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-CALENDAR-LEGACY "):
                self.note(line)

        done = "FOUNDATION-CALENDAR-LEGACY DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-CALENDAR-LEGACY DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-CALENDAR-LEGACY %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-CALENDAR-LEGACY RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-CALENDAR-LEGACY-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
