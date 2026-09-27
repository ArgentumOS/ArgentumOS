# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The formatter trio — §62.50's acceptance.

Apple added one class per KIND OF QUANTITY at 10.8: `NSLengthFormatter`, `NSMassFormatter`, `NSEnergyFormatter`,
each with an enum of the units it knows and four doors for converting, naming and rendering. This unit lands all
three, their three enums and fifteen unit cases — the largest named family in the re-ranked work list.

WHAT IS MEASURED IS THE ARITHMETIC AND THE CHOICES. A formatter that declared every unit and converted nothing
would pass a check that counted declarations, so every value is ABSOLUTE against the definition of the unit: 1500 m
is 1.5 km, half a kilogram is 500 g, a person 1.75 m tall is five feet and some inches. THE NATURAL-UNIT CHOOSER IS
MEASURED IN BOTH DIRECTIONS — 1500 m picks kilometres and 0.0005 m picks millimetres — because a chooser stuck at
one end of its table would satisfy half the checks and look right.

THE LOCALE STANCE IS NAMED RATHER THAN ASSUMED: this library formats with the root locale, so the default family is
METRIC and `forPersonHeightUse` / `forPersonMassUse` / `forFoodEnergyUse` are what ask for the other one. The last
check is the CONTROL — the same measurement without the flag is metric — so a flag that changed nothing would fail.

The probe is `/System/Shared/tests/foundation_quantity_formatters`, ONE unit, importing only `<Foundation/Foundation.h>`.

  * `the-three-formatters-are-declared-and-their-units-are-distinct`
  * `a-value-in-a-named-unit-renders-that-unit`
  * `a-number-formatter-set-is-the-one-returned`
  * `a-value-with-no-unit-gets-the-natural-one`
  * `the-three-quantities-use-their-own-base`
  * `each-person-flag-changes-the-family`
  * `without-the-flag-the-same-measurement-is-metric`
  * `an-unknown-unit-names-nothing-and-renders-a-bare-number`
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_quantity_formatters"
CHECKS = (
    "the-three-formatters-are-declared-and-their-units-are-distinct",
    "a-value-in-a-named-unit-renders-that-unit",
    "a-number-formatter-set-is-the-one-returned",
    "a-value-with-no-unit-gets-the-natural-one",
    "the-three-quantities-use-their-own-base",
    "each-person-flag-changes-the-family",
    "without-the-flag-the-same-measurement-is-metric",
    "an-unknown-unit-names-nothing-and-renders-a-bare-number",
)


class Case(BaseCase):
    title = "The formatter trio: units, conversions and the natural-unit choice (§62.50)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_quantity_formatters")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-QTYFMT-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-QTYFMT "):
                self.note(line)

        done = "FOUNDATION-QTYFMT DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-QTYFMT DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-QTYFMT %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-QTYFMT RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-QTYFMT-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
