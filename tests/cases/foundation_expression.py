# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSExpression — F13.10's acceptance for the expression tree.

docs/design/foundation-plan.md §10. F11 built `NSPredicate` as a tree of its own nodes; an expression
is the OTHER public half of that idea — a standalone value tree a caller can build, hand around,
compare and evaluate — and `NSComparisonPredicate` is what puts two of them together. The probe is
`/System/Shared/tests/foundation_expression`, ONE unit, importing only `<Foundation/Foundation.h>`.

  * `expr-constant`         — the type, the value and the evaluation of a constant;
  * `expr-keypath`          — an accessor reached BY NAME, the same through a DOTTED path, and nil
                              when there is no object to read it from;
  * `expr-evaluated-object` — `@SELF`, which is the object it is evaluated against;
  * `expr-variable-context` — a VARIABLE resolved from the context, and nil without one;
  * `expr-aggregate`        — a collection whose NESTED expressions are evaluated as it is;
  * `expr-set-operations`   — union/intersect/minus, including the rule that TWO SETS answer a SET
                              while anything else answers an ARRAY;
  * `expr-fold-functions`   — sum/count/min/max over a key path's collection;
  * `expr-equality`         — two separately-built trees that are equal, plus `-copy` being the
                              expression itself and `-description`.

NAMED LIMITS, asserted nowhere because they are not implemented: `+expressionForBlock:` and the
conditional/block types, `NSSubqueryExpressionType`, the two-argument functions
(`castObject:toType:`), and `@anyKey` — which exists as a TYPE but evaluates to nil, as Cocoa's own
documentation says its value is undefined.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_expression"
CHECKS = ("expr-constant", "expr-keypath", "expr-evaluated-object", "expr-variable-context",
          "expr-aggregate", "expr-set-operations", "expr-fold-functions",
          "expr-equality-and-description")


class Case(BaseCase):
    title = "NSExpression: a value described as a tree you can evaluate"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_expression")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-EXPRESSION-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-EXPRESSION "):
                self.note(line)

        done = "FOUNDATION-EXPRESSION DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-EXPRESSION DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-EXPRESSION %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-EXPRESSION RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-EXPRESSION-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
