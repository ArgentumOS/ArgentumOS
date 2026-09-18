# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSPredicate (the object model) — F11a's acceptance.

docs/design/foundation-plan.md §5 (F11). The family lands in two halves and this is the first:
the predicate OBJECT, which needs no parser. The probe is `/System/Shared/tests/foundation_predicate`,
built from two translation units; the support unit imports ONLY the umbrella header (so this also
proves NSPredicate reached it) and builds one of the predicates itself — a predicate is a VALUE,
so one made over there answers over here.

  * `pred-value`          — `+predicateWithValue:`, which does not care what it is asked about;
  * `pred-block`          — `+predicateWithBlock:`, and the documented half of Cocoa's shape that
                            is always nil (`bindings`);
  * `pred-and`            — the tree node: AND, and that the first NO DECIDES (the counting leaf
                            after it is never called);
  * `pred-or`             — OR, and that the first YES decides;
  * `pred-not`            — NOT, storing one child;
  * `pred-identities`     — AND of nothing is YES and OR of nothing is NO;
  * `pred-nested`         — a tree of trees, rendered;
  * `pred-filter`         — `-[NSArray filteredArrayUsingPredicate:]`: order kept, receiver
                            untouched;
  * `pred-filter-mutable` — `-[NSMutableArray filterUsingPredicate:]`, in place;
  * `pred-abstract`       — the base RAISES rather than answering a default that would be a lie;
  * `pred-nil-filter`     — a nil predicate raises rather than quietly answering an empty array;
  * `pred-refusals`       — NSExpression, NSComparisonPredicate and the format grammar are ABSENT;
  * `cross-tu`            — a predicate built in the other unit filters here.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_predicate"
CHECKS = ("pred-value", "pred-block", "pred-and", "pred-or", "pred-not",
          "pred-identities", "pred-nested", "pred-filter", "pred-filter-mutable",
          "pred-abstract", "pred-nil-filter", "pred-refusals", "cross-tu")


class Case(BaseCase):
    title = "NSPredicate: the predicate object — a tree that needs no parser"
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
