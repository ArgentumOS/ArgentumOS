# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""ICU on the guest — F13's acceptance for the bring-up.

docs/design/foundation-plan.md §10. The un-refusal program's FIRST slice, and the one that decides
whether the rest is even shaped right: ICU 76.1 is cross-built for musl/FNX, and this case asks the
guest to do what the Foundation's data-driven families will do — resolve libicuuc/libicui18n/
libicudata out of /System/Libraries and get REAL DATA out of them.

The probe is `/System/Shared/tests/icu_smoke`, a C program, and EVERY CHECK ASKS FOR A
DATA-DEPENDENT ANSWER so that nothing can pass from constants compiled into it:

  * `icu-version`    — the library loaded and names itself;
  * `icu-num-de`     — de_DE renders 1234567 as "1.234.567";
  * `icu-num-en`     — en_US renders the SAME number as "1,234,567" (two answers, one number);
  * `icu-num-ar`     — ar_EG renders it in NON-ASCII digits (asserted as a property, not a
                       hard-coded numeral — a table in the probe is what ICU is bound to avoid);
  * `icu-dat-pattern`— an explicit "yyyy-MM-dd" pattern in UTC gives "2021-03-04";
  * `icu-dat-locale` — the locale's OWN medium-date pattern (CLDR data) names the month and year;
  * `icu-coll-de`    — German groups 'ö' with 'o' at primary strength;
  * `icu-coll-sv`    — Swedish does NOT (one collation rule cannot produce both);
  * `icu-tz-ids`     — the time-zone ID set enumerates to more than a hundred ids, which is the
                       IANA database F7 refused by name as "the identifiers ARE the database".
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/icu_smoke"
CHECKS = ("icu-version", "icu-num-de", "icu-num-en", "icu-num-ar",
          "icu-dat-pattern", "icu-dat-locale", "icu-coll-de", "icu-coll-sv",
          "icu-tz-ids")


class Case(BaseCase):
    title = "ICU on the guest: the data-driven families' backend loads and answers"
    tier = "fast"
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("icu_smoke")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo ICU-SMOKE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("ICU-SMOKE "):
                self.note(line)

        done = "ICU-SMOKE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no ICU-SMOKE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^ICU-SMOKE %s ok$" % re.escape(c),
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

        tally = re.search(r"ICU-SMOKE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "ICU-SMOKE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
