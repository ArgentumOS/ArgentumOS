# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSMorphology + NSMorphologyCustomPronoun + NSMorphologyPronoun — §62.78's acceptance.

docs/design/foundation-plan.md §62.78. Thirty-seven rows: the three classes, five grammatical enums
(NSGrammaticalDefiniteness/Determination/Case/Person/PronounType) and their twenty-nine cases.

The morphology half of `Fundamentals / Automatic grammar agreement`. These are VALUE types: what is real is the
vocabulary carried as values — fields set, copied, compared, archived and asked whether they say anything —
because there is NO MORPHOLOGY ENGINE in this system and the header says so.

The probe is `/System/Shared/tests/foundation_morphology`, ONE unit, importing only
`<Foundation/Foundation.h>`.

  * `the-five-new-enums-have-notset-at-zero-and-distinct-cases` — the rule that makes a fresh value mean "nothing
                                     said";
  * `a-fresh-morphology-is-unspecified-in-every-field` — and this check FOUND A REAL BUG: the gender enum that
                                     predates the class puts NotSet at THREE, so a zeroed allocation read as
                                     `feminine` until the class grew an -init that names its unset cases;
  * `the-eight-fields-round-trip-and-stop-being-unspecified`;
  * `a-copy-is-a-value-and-not-a-reference` / `equality-follows-the-fields`;
  * `a-morphology-round-trips-through-a-keyed-archiver` / `a-pronoun-round-trips-through-a-keyed-archiver`;
  * `the-user-morphology-is-unspecified-and-fresh-each-call` — the door that ANSWERS rather than refusing, because
                                     a system with no user morphology has a user whose morphology is unspecified;
  * `the-per-language-pronoun-door-refuses-by-name` — refused BY NAME with the language in the error, rather than
                                     accepting a value nothing can read back;
  * `a-custom-pronoun-carries-five-forms-and-copies-as-a-value` — plus the honest NO from
                                     `+isSupportedForLanguage:`;
  * `a-pronoun-carries-its-own-and-its-dependents-morphology` / `a-pronoun-with-no-word-is-refused-at-the-door`.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_morphology"
CHECKS = ("the-five-new-enums-have-notset-at-zero-and-distinct-cases",
          "a-fresh-morphology-is-unspecified-in-every-field",
          "the-eight-fields-round-trip-and-stop-being-unspecified",
          "a-copy-is-a-value-and-not-a-reference",
          "equality-follows-the-fields",
          "a-morphology-round-trips-through-a-keyed-archiver",
          "the-user-morphology-is-unspecified-and-fresh-each-call",
          "the-per-language-pronoun-door-refuses-by-name",
          "a-custom-pronoun-carries-five-forms-and-copies-as-a-value",
          "a-pronoun-carries-its-own-and-its-dependents-morphology",
          "a-pronoun-round-trips-through-a-keyed-archiver",
          "a-pronoun-with-no-word-is-refused-at-the-door")


class Case(BaseCase):
    title = "NSMorphology: the grammatical vocabulary as values"
    tier = "fast"
    # No fixture and no file system work: it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_morphology")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-MORPHOLOGY-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-MORPHOLOGY "):
                self.note(line)

        done = "FOUNDATION-MORPHOLOGY DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-MORPHOLOGY DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-MORPHOLOGY %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-MORPHOLOGY RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-MORPHOLOGY-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
