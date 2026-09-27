# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSLinguisticTagger — §62.42's acceptance, and the largest single family on §62.24's work list.

One deprecated class and FIFTY-ONE VOCABULARY NAMES. The vocabulary is pinned IN FULL rather than sampled: the tag
values are this library's under §11.6.1 D2, so the contract they carry is between the header and the probe, and the
property a caller's switch depends on is that every constant exists and is DISTINCT within its scheme.

WHAT IS IMPLEMENTED is the half this system can do honestly — tokenization over ICU's break iterator, the
TokenType scheme (with the refined punctuation tags the vocabulary already carries) and the Script scheme. WHAT IS
REFUSED is refused by name AND MEASURED: the morphological schemes (LexicalClass, NameType,
NameTypeOrLexicalClass, Lemma) and Language answer NOTHING, and `JoinNames` changes no result.

The probe is `/System/Shared/tests/foundation_linguistictagger`, ONE unit, importing only
`<Foundation/Foundation.h>`.

  * `the-seven-schemes-are-pinned`                 — and the two spellings a caller compares against;
  * `the-token-types-and-their-refinements-are-pinned` — fifteen tags, distinct within their scheme;
  * `the-lexical-classes-and-the-name-types-are-pinned` — the refused schemes' vocabulary, still named;
  * `the-four-units-are-pinned`                    — the enum's values, ours under D2;
  * `the-five-options-are-single-bits`             — a mask of five distinct bits;
  * `available-schemes-is-the-honest-list`         — what can be asked, readable before anything is built;
  * `the-token-types-are-classified`               — a real sentence's sequence;
  * `the-refined-punctuation-has-its-own-tags`     — quotes, parentheses, dash;
  * `the-script-is-icus-short-name`                — Latin and Han;
  * `the-morphological-schemes-answer-nothing`     — the SAME token: Word beside five nils;
  * `the-omissions-drop-what-they-say`             — the four omissions, and JoinNames changing nothing;
  * `the-sentence-range-is-the-sentence`           — a break-iterator question, answered;
  * `the-orthography-doors-round-trip`             — the class §62.21 already ships.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_linguistictagger"
CHECKS = (
        "the-seven-schemes-are-pinned",
        "the-token-types-and-their-refinements-are-pinned",
        "the-lexical-classes-and-the-name-types-are-pinned",
        "the-four-units-are-pinned",
        "the-five-options-are-single-bits",
        "available-schemes-is-the-honest-list",
        "the-token-types-are-classified",
        "the-refined-punctuation-has-its-own-tags",
        "the-script-is-icus-short-name",
        "the-morphological-schemes-answer-nothing",
        "the-omissions-drop-what-they-say",
        "the-sentence-range-is-the-sentence",
        "the-orthography-doors-round-trip",
)


class Case(BaseCase):
    title = "NSLinguisticTagger: the deprecated class and its fifty-one names (§62.42)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_linguistictagger")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-LINGUISTICTAGGER-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-LINGUISTICTAGGER "):
                self.note(line)

        done = "FOUNDATION-LINGUISTICTAGGER DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-LINGUISTICTAGGER DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-LINGUISTICTAGGER %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-LINGUISTICTAGGER RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-LINGUISTICTAGGER-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
