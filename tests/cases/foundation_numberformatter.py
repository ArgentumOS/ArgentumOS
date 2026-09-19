# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSNumberFormatter — F13.7c's acceptance, the second un-refused data family.

docs/design/foundation-plan.md §10. F7 refused "the parser and formatter family" because the formats
ARE a table; ICU is a dependency now, so the table ships and this class reads it. Nothing in the
class encodes a format.

The probe is `/System/Shared/tests/foundation_numberformatter`, ONE unit (the claim is data, not a
cross-translation-unit boundary), importing only `<foundation/Foundation.h>`.

  * `nf-de` / `nf-en`     — THE SAME NUMBER IN TWO LOCALES: 1234567.89 is "1.234.567,89" in de_DE
                            and "1,234,567.89" in en_US;
  * `nf-currency-locale`  — the currency style: the locale's symbol and placement ("$…" vs "… €");
  * `nf-spellout`         — 42 is "forty-two" (a rule of language, not arithmetic);
  * `nf-ordinal`          — 3 is "3rd";
  * `nf-percent`          — 0.25 is "25%";
  * `nf-scientific`       — the scientific style uses an exponent;
  * `nf-parse`            — "1.234.567,89" parses in the locale's conventions;
  * `nf-round-trip`       — format then parse returns the same number;
  * `nf-int64-exact`      — 9007199254740993 (2^53+1) survives: a double CANNOT hold it, so this is
                            what keeps the 64-bit door honest;
  * `nf-symbols-custom`   — a symbol SET on the formatter reaches the output;
  * `nf-allowsfloats-off` — a fraction is refused when -allowsFloats is NO;
  * `nf-zero-and-nil`     — the two symbols that are OURS (ICU has neither);
  * `nf-formatter-door`   — NSNumberFormatter answers the NSFormatter door too;
  * `nf-copy-independent` — a copy is independent of its original.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_numberformatter"
CHECKS = ("nf-de", "nf-en", "nf-currency-locale", "nf-spellout", "nf-ordinal",
          "nf-percent", "nf-scientific", "nf-parse", "nf-round-trip",
          "nf-int64-exact", "nf-symbols-custom", "nf-allowsfloats-off",
          "nf-zero-and-nil", "nf-formatter-door", "nf-copy-independent")


class Case(BaseCase):
    title = "NSNumberFormatter: a locale's numbers, on ICU"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_numberformatter")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-NUMBERFORMATTER-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-NUMBERFORMATTER "):
                self.note(line)

        done = "FOUNDATION-NUMBERFORMATTER DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no DONE marker; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-NUMBERFORMATTER %s ok$" % re.escape(c),
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

        tally = re.search(r"FOUNDATION-NUMBERFORMATTER RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-NUMBERFORMATTER-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
