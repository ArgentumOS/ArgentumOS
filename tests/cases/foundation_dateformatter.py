# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSDateFormatter — F13.6's acceptance, the first un-refused data family.

docs/design/foundation-plan.md §10. F7 refused "the parser and formatter family" because the
formats ARE a table (CLDR patterns, month names, a locale's field order). ICU is now a dependency,
so the table ships — it is just not ours to write — and this case is what says so out loud.

The probe is `/System/Shared/tests/foundation_dateformatter`, ONE unit (unlike the other Foundation
probes): their claim is a cross-translation-unit boundary, while this family's claim is DATA coming
back through Foundation's own API. It imports only `<foundation/Foundation.h>`.

  * `df-style-medium-en`   — a locale's MEDIUM date names the month in that locale;
  * `df-style-long-locale` — the same instant is "März" in de_DE and "March" in en_US;
  * `df-pattern-format`    — an explicit "dd.MM.yyyy" pattern formats the fixed instant;
  * `df-round-trip`        — format then parse returns the same instant;
  * `df-parse`             — parsing de_DE text with a de_DE pattern gives the exact instant;
  * `df-timezone-offset`   — the zone crosses as an OFFSET: 00:00 at +00:00, 05:30 at +05:30,
                             16:00 at -08:00 for one instant;
  * `df-template-order`    — CLDR skeletons: @"yMMMd" returns a PATTERN whose order is the
                             locale's (en month-first, de day-first);
  * `df-no-fields`         — a formatter with nothing to say answers "" (not an error);
  * `df-parse-refusal`     — text that is not a date does not crash the parser;
  * `df-strict-parse`      — strict refuses an impossible field, lenient does not;
  * `df-base-raises`       — NSFormatter, the abstract base, refuses to invent a format;
  * `df-copy-independent`  — a copy is independent of its original.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_dateformatter"
CHECKS = ("df-style-medium-en", "df-style-long-locale", "df-pattern-format",
          "df-round-trip", "df-parse", "df-timezone-offset", "df-template-order",
          "df-no-fields", "df-parse-refusal", "df-strict-parse",
          "df-base-raises", "df-copy-independent", "df-named-zone-dst",
          "df-symbols", "df-calendar-hebrew", "df-template-set")


class Case(BaseCase):
    title = "NSDateFormatter: the first un-refused data family, on ICU"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_dateformatter")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-DATEFORMATTER-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-DATEFORMATTER "):
                self.note(line)

        done = "FOUNDATION-DATEFORMATTER DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no DONE marker; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-DATEFORMATTER %s ok$" % re.escape(c),
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

        tally = re.search(r"FOUNDATION-DATEFORMATTER RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-DATEFORMATTER-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
