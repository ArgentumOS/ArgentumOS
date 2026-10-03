# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSCalendarDate — §62.67's acceptance, and the last class row of Fundamentals/Deprecated.

docs/design/foundation-plan.md §62.67. Apple-deprecated and shipping anyway (§62.24: deprecated API is a porting
target, and a surface built so that an older application compiles is defeated by excluding what it calls).

The class is an NSDate SUBCLASS whose fields are computed rather than stored: NSDate's own instant, read in the
RECEIVER'S zone, plus the two things this older API carried around an instant — a CALENDAR FORMAT (strftime /
strptime spelling) and a TIME ZONE.

The probe is `/System/Shared/tests/foundation_calendardate`, ONE unit, importing only
`<Foundation/Foundation.h>`.

EVERY CHECK USES A FIXED-OFFSET ZONE on purpose: a named zone's answer depends on a daylight-saving database and
on the instant it is asked about, so a probe asserting fields in "Europe/London" would be asserting ICU's data.
One check does use the SYSTEM zone and asserts only what must hold in any zone.

  * `the-format-and-the-zone-are-kept-and-settable` — the two decisions a new date carries;
  * `the-fields-are-the-numbers-it-was-built-from` — 2026-09-27 13:45:30 built from numbers reads back so;
  * `the-day-numbers-are-one-based-and-the-weekday-is-sunday-zero` — the 270th day, a Sunday;
  * `the-zone-moves-every-field-and-not-the-instant` — computed, not stored, in one assertion;
  * `a-date-built-in-the-system-zone-holds-the-instant-it-prints` — true in any zone;
  * `the-format-prints-and-parses-back` — a zone-less string round-trips its FIELDS (read in the reader's zone);
  * `a-string-that-does-not-match-the-format-answers-nil` — the parse door's refusal;
  * `the-default-format-is-what-a-new-date-prints-and-parses` — ours (§11.6.1 D2), and its `%z` makes the
                                     INSTANT round-trip: the printed offset is one of the four defects this probe
                                     found (gmtime_r leaves tm_gmtoff at zero, so a UTC+1 date printed "+0000");
  * `adding-one-month-to-the-31st-lands-in-february` — calendar-aware addition, clamped: 31 Jan + 1 month is
                                     28 Feb, not 2 or 3 March;
  * `the-decomposition-is-largest-component-first` — and it INVERTS the addition (2 months, 5 days, 1 hour);
  * `the-day-of-the-common-era-counts-from-year-one` — 1970-01-01 is day 719163;
  * `the-constant-dates-are-calendar-dates` — +distantPast/+distantFuture answer THIS class.

FOUR DEFECTS THE PROBE CAUGHT, all in this class's own arithmetic rather than in the C library: a bogus year
normalisation that made 2026 parse as 226; the carry-versus-clamp choice for a month landing on a day that does
not exist; tm_gmtoff left at UTC by gmtime_r, so `%z` printed the wrong zone; and timegm overwriting the very
tm_gmtoff the parse had just read, in an expression C is free to order either way.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_calendardate"
CHECKS = (
    "timezone-default-is-settable-and-answers-what-was-set",
    "timezone-fixed-offset-zone-has-no-daylight-saving",
    "timezone-localized-name-differs-by-style",
    "timezone-abbreviation-answers-the-zone-s-own-name","the-format-and-the-zone-are-kept-and-settable",
          "the-fields-are-the-numbers-it-was-built-from",
          "the-day-numbers-are-one-based-and-the-weekday-is-sunday-zero",
          "the-zone-moves-every-field-and-not-the-instant",
          "a-date-built-in-the-system-zone-holds-the-instant-it-prints",
          "the-format-prints-and-parses-back",
          "a-string-that-does-not-match-the-format-answers-nil",
          "the-default-format-is-what-a-new-date-prints-and-parses",
          "adding-one-month-to-the-31st-lands-in-february",
          "the-decomposition-is-largest-component-first",
          "the-day-of-the-common-era-counts-from-year-one",
          "the-constant-dates-are-calendar-dates")


class Case(BaseCase):
    title = "NSCalendarDate: an instant with a format and a zone"
    tier = "fast"
    # No fixture and no file system work: it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_calendardate")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-CALENDARDATE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-CALENDARDATE "):
                self.note(line)

        done = "FOUNDATION-CALENDARDATE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-CALENDARDATE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-CALENDARDATE %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-CALENDARDATE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-CALENDARDATE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
