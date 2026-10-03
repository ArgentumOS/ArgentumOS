# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The calendar family — F7's acceptance.

docs/design/foundation-plan.md §5 (F7). The probe is
`/System/Shared/tests/foundation_calendar`, built from two translation units; the
support unit imports ONLY the umbrella header, so a complete
`<Foundation/Foundation.h>` — including the three headers F7 adds — is part of
what is being checked.

  * `tz-offset`          — a fixed offset: its seconds, its rendered name, that
                           it has no DST, and equality by offset;
  * `calendar-convert`   — an absolute time to fields, against known dates;
  * `calendar-roundtrip` — fields -> date -> the same fields;
  * `calendar-add-months`— THE CLAMP: 31 January + 1 month is the last day of
                           February, 28 or 29 by year;
  * `calendar-add-units` — day/year arithmetic across a month end;
  * `calendar-ranges`    — days in a month, months in a year, hours in a day;
  * `calendar-weeks`     — the week rule (firstWeekday + minimumDaysInFirstWeek)
                           and the year that owns week 1;
  * `calendar-timezone`  — the same instant is a different local date in +05:30;
  * `calendar-refusals`  — what is ABSENT is absent: no tz database by NAME, no
                           non-Gregorian calendar (the `-dateFromString:` assertion is on
                           NSCalendar, which has no parser in Cocoa either);
  * `calendar-formatter-present` — and POSITIVELY: the parser/formatter family that this probe
                           used to describe as refused came back in F13.6, so it is asserted
                           present here. The pair is the point.
  * `cross-tu`           — objects built in the other unit behave locally.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_calendar"
CHECKS = ("tz-offset", "calendar-convert", "calendar-roundtrip",
          "calendar-add-months", "calendar-add-units", "calendar-ranges",
          "calendar-weeks", "calendar-timezone", "calendar-refusals",
          "calendar-non-gregorian", "calendar-difference", "tz-names-present",
          "calendar-formatter-present", "cross-tu", "calendar-alias-and-wire-values",
          "calendar-extraction", "calendar-date-with-week", "calendar-range-and-ordinality",
          "calendar-setting-and-granularity", "calendar-today-and-weekend",
          "calendar-symbols", "calendar-matches-and-comp-diff", "datecomponents-unit-accessors",
          "datecomponents-calendar-and-date", "datecomponents-invalid-day")


class Case(BaseCase):
    title = "the calendar family: NSCalendar, NSTimeZone, NSDateComponents"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_calendar")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-CALENDAR-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-CALENDAR "):
                self.note(line)

        done = "FOUNDATION-CALENDAR DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-CALENDAR DONE; output tail: "
                        + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-CALENDAR %s ok$" % re.escape(c),
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

        tally = re.search(r"FOUNDATION-CALENDAR RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-CALENDAR-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
