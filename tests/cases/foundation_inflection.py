# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSInflectionRule + NSInflectionRuleExplicit + NSTermOfAddress — §62.79's acceptance.

docs/design/foundation-plan.md §62.79. Three rows, and with §62.78's three classes they COMPLETE
`Fundamentals / Automatic grammar agreement`.

The rule/address half of the family, and the same shape as the morphology half: what is real is the VALUE — a rule
that carries a morphology, a term of address that carries a language and a pronoun list, each copied, compared and
archived — and what is absent is the ENGINE, which the two capability doors report as a fact about this system.

The probe is `/System/Shared/tests/foundation_inflection`, ONE unit, importing only
`<Foundation/Foundation.h>`.

  * `the-capability-doors-answer-no-for-any-language` — `+canInflectLanguage:` and
                                     `+canInflectPreferredLocalization` answer NO because nothing here agrees a
                                     grammar, which is a fact rather than an unimplemented door;
  * `the-automatic-rule-is-a-value-and-fresh-each-call`;
  * `an-explicit-rule-carries-a-copy-of-its-morphology` — and is not equal to the automatic rule;
  * `an-explicit-rule-round-trips-through-a-keyed-archiver`;
  * `the-predefined-terms-are-distinct-values-with-nothing-stated`;
  * `the-predefined-terms-are-equal-because-the-pronoun-data-is-absent` — THE BOUNDARY MADE VISIBLE where a reader
                                     can see it: the three terms differ only by the pronoun list this system does
                                     not carry, so this check asserts that they are equal rather than leaving the
                                     gap behind a passing check;
  * `a-localized-term-carries-its-language-and-a-copy-of-its-pronouns` — the one door that builds a term from real
                                     data, and it snapshots what it is given;
  * `a-localized-term-round-trips-through-a-keyed-archiver`;
  * `terms-compare-by-their-fields` — equal when language and pronouns match, unequal against a predefined term
                                     and against another language;
  * `a-localized-term-refuses-a-missing-language`.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_inflection"
CHECKS = ("the-capability-doors-answer-no-for-any-language",
          "the-automatic-rule-is-a-value-and-fresh-each-call",
          "an-explicit-rule-carries-a-copy-of-its-morphology",
          "an-explicit-rule-round-trips-through-a-keyed-archiver",
          "the-predefined-terms-are-distinct-values-with-nothing-stated",
          "the-predefined-terms-are-equal-because-the-pronoun-data-is-absent",
          "a-localized-term-carries-its-language-and-a-copy-of-its-pronouns",
          "a-localized-term-round-trips-through-a-keyed-archiver",
          "terms-compare-by-their-fields",
          "a-localized-term-refuses-a-missing-language")


class Case(BaseCase):
    title = "NSInflectionRule + NSTermOfAddress: the rule and the term of address, as values"
    tier = "fast"
    # No fixture and no file system work: it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_inflection")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-INFLECTION-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-INFLECTION "):
                self.note(line)

        done = "FOUNDATION-INFLECTION DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-INFLECTION DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-INFLECTION %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-INFLECTION RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-INFLECTION-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
