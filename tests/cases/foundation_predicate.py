# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSPredicate — F11a's object model AND F11b's format grammar.

docs/design/foundation-plan.md §5 (F11). The family lands in two halves. F11a is the predicate
OBJECT, which needs no parser; F11b is the FORMAT GRAMMAR, which parses a string into exactly
that object. The probe is `/System/Shared/tests/foundation_predicate`, built from two
translation units; the support unit imports ONLY the umbrella header (so this also proves
NSPredicate reached it) and builds one of the predicates itself — a predicate is a VALUE, so one
made over there answers over here.

THE OBJECT (F11a):

  * `pred-value`          — `+predicateWithValue:`, which does not care what it is asked about;
  * `pred-block`          — `+predicateWithBlock:`, and the documented half of Cocoa's shape that
                            is always nil (`bindings`);
  * `pred-and`            — the tree node: AND measured from BOTH sides (a FALSE first asks no
                            leaf; a TRUE first asks exactly one);
  * `pred-or`             — OR, and that the first YES decides;
  * `pred-not`            — NOT, storing one child;
  * `pred-identities`     — AND of nothing is YES and OR of nothing is NO;
  * `pred-nested`         — a tree of trees, rendered;
  * `pred-filter`         — `-[NSArray filteredArrayUsingPredicate:]`: order kept, receiver
                            untouched;
  * `pred-filter-mutable` — `-[NSMutableArray filterUsingPredicate:]`, in place;
  * `pred-abstract`       — the base RAISES rather than answering a default that would be a lie;
  * `pred-nil-filter`     — a nil predicate raises rather than quietly answering an empty array;
  * `pred-refusals`       — NSExpression, NSComparisonPredicate and substitution are ABSENT, and
                            the format grammar is PRESENT;
  * `cross-tu`            — a predicate built in the other unit filters here.

THE GRAMMAR (F11b) — the objects are DICTIONARIES, so a key path exercises KVC's dictionary form:

  * `format-compare`      — `=`, `>`, and a key path resolved through KVC;
  * `format-string-ops`   — CONTAINS, BEGINSWITH, ENDSWITH;
  * `format-like`         — `*` and `?`, and `\\*` as a LITERAL star (the escape a naive matcher
                            gets wrong);
  * `format-case`         — `[c]`, and that it survives the rendering;
  * `format-connectives`  — NOT/AND/OR with their PRECEDENCE;
  * `format-constants`    — TRUEPREDICATE, FALSEPREDICATE, and NULL against a missing key;
  * `format-self`         — SELF compared with a number;
  * `format-numbers`      — integers, fractions and a negative;
  * `format-round-trip`   — parse → render → parse, and the SAME answers from both trees;
  * `format-refusals`     — MATCHES, `[d]`, IN, ANY, `$` and a truncated format all RAISE, and the
                            messages NAME what was refused;
  * `format-filter`       — a predicate WRITTEN as text, used to filter — the two halves joined.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_predicate"
CHECKS = ("pred-value", "pred-block", "pred-and", "pred-or", "pred-not",
          "pred-identities", "pred-nested", "pred-filter", "pred-filter-mutable",
          "pred-abstract", "pred-nil-filter", "pred-refusals", "cross-tu",
          "format-compare", "format-string-ops", "format-like", "format-case",
          "format-connectives", "format-constants", "format-self", "format-numbers",
          "format-round-trip", "format-refusals", "format-matches",
          "format-diacritic", "format-regex-refusal", "format-filter")


class Case(BaseCase):
    title = "NSPredicate: the predicate object, and the format grammar that writes one"
    tier = "fast"
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_predicate")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-PREDICATE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-PREDICATE "):
                self.note(line)

        done = "FOUNDATION-PREDICATE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-PREDICATE DONE; output tail: "
                        + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-PREDICATE %s ok$" % re.escape(c),
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

        tally = re.search(r"FOUNDATION-PREDICATE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-PREDICATE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
