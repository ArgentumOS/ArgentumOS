# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSDataDetector — three detectors, two refusals, and one rule for a span that two detectors want.

The probe is `/System/Shared/tests/foundation_datadetector`, ONE unit importing only
`<Foundation/Foundation.h>`.

THE REFUSALS ARE THE CONTRACT, NOT A GAP. Asking for a type that no data detector makes (Spelling, Grammar,
Quote, Dash, Replacement, Correction, Orthography, RegularExpression) is an error on Apple's own page. Asking for
Address or TransitInformation is an error HERE because the data does not exist on this system — a postal-address
grammar and an airline schedule — and those two are register entries D15/D16 under §11.6's ground (i). Both checks
read the error's DOMAIN and CODE rather than only that the call failed.

  * `a-type-no-detector-makes-is-refused-with-an-error`, `address-and-transit-refuse-by-name`,
    `asking-for-no-type-is-refused`, `the-three-supported-types-are-accepted`;
  * `a-link-is-found-with-its-range-and-its-url`, `a-bare-host-is-a-link-and-the-url-gets-a-scheme`,
    `trailing-punctuation-is-not-part-of-the-link`,
    `a-balanced-parenthesis-stays-and-an-unbalanced-one-does-not`;
  * `an-iso-date-is-found-and-its-day-is-the-one-written` — the DAY is read back through NSCalendar, so what is
    checked is the date the calendar reports and not a string this probe would have had to build;
  * `the-three-other-date-forms-all-name-the-same-day`, `a-day-the-calendar-does-not-have-is-not-a-date`
    (February 30 is the calendar's own refusal), `a-date-and-a-time-joined-by-a-space-are-one-result`,
    `a-time-on-its-own-is-today-at-that-time`;
  * `a-phone-number-is-found-in-both-international-shapes`, `a-bare-digit-run-is-not-a-phone-number`;
  * `one-span-is-claimed-once-and-the-date-wins-it`, `a-date-inside-a-link-is-part-of-the-link`,
    `the-answer-is-in-document-order-and-never-overlaps`, `the-range-argument-bounds-the-search`;
  * `the-parents-matching-doors-work-and-the-block-enumerator-too`, `a-detector-is-not-a-pattern`
    (it answers nil for `-pattern`), `a-detector-is-immutable-and-compares-by-its-types`.

EVERY CHECK BUILDS ITS OWN DETECTOR, asking only for the kind it is about: `2026-02-30` is a hyphenated eight-digit
run as well as a date that does not exist, so a check about the DATE detector must not be able to answer
differently because the phone detector is also switched on. A check whose own precondition other checks can change
measures its neighbours.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_datadetector"
CHECKS = ("a-type-no-detector-makes-is-refused-with-an-error",
          "address-and-transit-refuse-by-name",
          "asking-for-no-type-is-refused",
          "the-three-supported-types-are-accepted",
          "a-link-is-found-with-its-range-and-its-url",
          "a-bare-host-is-a-link-and-the-url-gets-a-scheme",
          "trailing-punctuation-is-not-part-of-the-link",
          "a-balanced-parenthesis-stays-and-an-unbalanced-one-does-not",
          "an-iso-date-is-found-and-its-day-is-the-one-written",
          "the-three-other-date-forms-all-name-the-same-day",
          "a-day-the-calendar-does-not-have-is-not-a-date",
          "a-date-and-a-time-joined-by-a-space-are-one-result",
          "a-time-on-its-own-is-today-at-that-time",
          "a-phone-number-is-found-in-both-international-shapes",
          "a-bare-digit-run-is-not-a-phone-number",
          "one-span-is-claimed-once-and-the-date-wins-it",
          "a-date-inside-a-link-is-part-of-the-link",
          "the-answer-is-in-document-order-and-never-overlaps",
          "the-range-argument-bounds-the-search",
          "the-parents-matching-doors-work-and-the-block-enumerator-too",
          "a-detector-is-not-a-pattern",
          "a-detector-is-immutable-and-compares-by-its-types")


class Case(BaseCase):
    title = "NSDataDetector: links, dates and phone numbers, and the two kinds this system has no data for"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_datadetector")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-DATADETECTOR-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-DATADETECTOR "):
                self.note(line)

        done = "FOUNDATION-DATADETECTOR DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-DATADETECTOR DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-DATADETECTOR %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-DATADETECTOR RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-DATADETECTOR-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
