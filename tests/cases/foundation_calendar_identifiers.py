# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The pre-10.9 calendar identifiers — §62.49's acceptance.

Apple deprecated `NSGregorianCalendar` and its ten siblings at 10.9 in favour of the `NSCalendarIdentifier*`
spelling; §62.24's policy puts them back. This is the §62.46 pattern one level over: not cases of an enum but
`NSString *const` keys, and the reason they are MACROS rather than second strings is the property this case checks.

THE PROPERTY IS IDENTITY, NOT EQUALITY. A calendar identifier is a key: a program handing `NSGregorianCalendar` to
`+[NSCalendar calendarWithIdentifier:]` and one handing the modern name must reach the same calendar, and a restored
constant built as a second string with the same characters would be a different object doing the job by accident.
So the first check is a POINTER comparison, and the last is the CONTROL — two neighbouring names must really be two
calendars (the Buddhist era is 2569 where the Gregorian is 2026), because three checks about a single object would
all pass if every name had collapsed into one.

The probe is `/System/Shared/tests/foundation_calendar_identifiers`, ONE unit, importing only `<Foundation/Foundation.h>`.

  * `each-legacy-name-is-the-same-object-as-its-modern-identifier` — eleven pointer comparisons;
  * `the-eleven-names-are-eleven-calendars`          — a name pointing at the wrong identifier shows here;
  * `a-legacy-name-does-the-job-the-modern-name-does` — the same date answered the same way (the library has
    no `-calendarIdentifier`, so the property is measured through behaviour instead);
  * `two-neighbouring-names-are-two-calendars`       — the control: an alias that lost its meaning fails only here.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_calendar_identifiers"
CHECKS = ("each-legacy-name-is-the-same-object-as-its-modern-identifier",
          "the-eleven-names-are-eleven-calendars",
          "a-legacy-name-does-the-job-the-modern-name-does",
          "two-neighbouring-names-are-two-calendars")


class Case(BaseCase):
    title = "The pre-10.9 calendar identifiers are the modern identifiers (§62.49)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_calendar_identifiers")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-CALENDAR-IDS-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-CALENDAR-IDS "):
                self.note(line)

        done = "FOUNDATION-CALENDAR-IDS DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-CALENDAR-IDS DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-CALENDAR-IDS %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-CALENDAR-IDS RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-CALENDAR-IDS-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
