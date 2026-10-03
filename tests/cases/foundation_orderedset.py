# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSOrderedSet / NSMutableOrderedSet — F13.8e's acceptance.

docs/design/foundation-plan.md §10. An ordered set is neither an `NSSet` nor an `NSArray`: it holds
each value once AND the order is part of the value. The probe is
`/System/Shared/tests/foundation_orderedset`, ONE unit, importing only `<Foundation/Foundation.h>`.

  * `ordered-keeps-the-order-it-was-given` — the duplicate is in the MIDDLE, so a build keeping the
                                    last occurrence instead of the first lands in the wrong position;
  * `ordered-index-and-lookup`            — positional access, `-firstObject`/`-lastObject`, and an
                                    `-indexOfObject:` that finds a DISTINCT but equal string by value;
  * `ordered-lends-its-membership-to-a-set` — `-set` is a real `NSSet` over the same members;
  * `ordered-equality-is-order-sensitive` — THE MEASUREMENT THAT EARNS ITS PLACE: the same members in
                                    a different order are NOT equal, while their `-set` views ARE —
                                    so the inequality is about ORDER, not about contents;
  * `ordered-relations`                  — `-isSubsetOfOrderedSet:` / `-intersectsOrderedSet:`;
  * `ordered-mutation-keeps-the-order`   — add (a duplicate is ignored), insert at 0, replace,
                                    exchange and remove, asserted POSITION BY POSITION;
  * `ordered-enumeration-both-ways`      — for-in in order, and `-reverseObjectEnumerator`, as the
                                    literal strings `cab` and `bac`;
  * `ordered-set-sorted-by-descriptor`         — the F11 predicate family (order preserved) and F10's
                                    sort descriptors;
  * `ordered-copy-semantics`             — a mutable's `-copy` is an immutable snapshot.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_orderedset"
CHECKS = ("ordered-keeps-the-order-it-was-given", "ordered-index-and-lookup",
          "ordered-lends-its-membership-to-a-set", "ordered-equality-is-order-sensitive",
          "ordered-relations", "ordered-mutation-keeps-the-order",
          "ordered-enumeration-both-ways", "ordered-set-sorted-by-descriptor",
          "ordered-copy-semantics", "ordered-construction-doors",
          "ordered-construction-copies-when-asked", "ordered-enumeration-doors",
          "ordered-positional-and-reversal", "ordered-set-relations",
          "ordered-predicate-and-comparator-doors", "ordered-mutable-sorts",
          "ordered-set-and-count-mutators", "ordered-nscoding-doors")


class Case(BaseCase):
    title = "NSOrderedSet: a set whose order is part of its value"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_orderedset")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-ORDEREDSET-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-ORDEREDSET "):
                self.note(line)

        done = "FOUNDATION-ORDEREDSET DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-ORDEREDSET DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-ORDEREDSET %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-ORDEREDSET RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-ORDEREDSET-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
