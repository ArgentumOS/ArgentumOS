# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSSet / NSMutableSet — F13.8's acceptance, the first family the boundary never justified.

docs/design/foundation-plan.md §10. Every other entry in the plan's refusal table was refused for a
REASON (a table, a service, an evaluator); a set was never refused — it was simply missing, and the
plan's own KVC section says so in passing ("the set-returning operators need an NSSet that does not
exist here").

The probe is `/System/Shared/tests/foundation_set`, ONE unit (the claim is VALUE SEMANTICS rather
than a cross-translation-unit boundary), importing only `<Foundation/Foundation.h>`.

  * `set-dedupes-by-value`  — two DISTINCT NSString objects with the same characters are ONE member;
  * `set-member-by-value`   — `-member:` finds the stored object from a fresh equal one;
  * `set-algebra`           — union / minus / intersect, by membership and count;
  * `set-relations`         — isEqualToSet: / isSubsetOfSet: / intersectsSet:;
  * `set-adding-forms`      — the three `-setByAdding…` forms, and the receiver left UNCHANGED;
  * `set-enumeration`       — for-in and -enumerateObjectsUsingBlock: each visit every member once;
  * `set-order-independent` — sets built in DIFFERENT ORDERS are equal AND hash alike;
  * `set-sort-descriptors`  — a set becomes an ORDERED array through NSSortDescriptor (F10);
  * `set-copy-semantics`    — a mutable set's -copy is an immutable snapshot of the same membership;
  * `set-counted`          — NSCountedSet: `-count` is DISTINCT members while `-countForObject:` is
                        how many times one was added, counting by VALUE (a distinct but equal
                        object increments the same count), and an array of duplicates arrives with
                        its multiplicities intact.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_set"
CHECKS = ("set-dedupes-by-value", "set-member-by-value", "set-algebra",
          "set-relations", "set-adding-forms", "set-enumeration",
          "set-order-independent", "set-sort-descriptors",
          "set-copy-semantics", "set-counted", "set-variadic-factory", "set-nscoding-doors")


class Case(BaseCase):
    title = "NSSet: the unordered collection the boundary never explained"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_set")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-SET-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-SET "):
                self.note(line)

        done = "FOUNDATION-SET DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-SET DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-SET %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-SET RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-SET-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
