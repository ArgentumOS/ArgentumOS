# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSSortDescriptor — F10's acceptance.

docs/design/foundation-plan.md §5 (F10). The probe is
`/System/Shared/tests/foundation_sort`, built from two translation units; the support unit
imports ONLY the umbrella header (so this also proves NSSortDescriptor reached it), defines the
objects, and builds one of the descriptors. The split is the family's own claim: a sort whose
key was resolved BY NAME can only have been resolved through the runtime — through KVC (F9).

  * `sort-descriptor` — the value: key, direction, and which comparison kind;
  * `sort-reversed`   — `-reversedSortDescriptor` flips the direction and nothing else;
  * `sort-array`      — a sort whose key is resolved by name, the receiver left untouched;
  * `sort-chain`      — a chain is lexicographic: the first decides, a TIE falls to the next;
  * `sort-stable`     — equal keys keep their INPUT order (the promise, measured — an unstable
                        sort passes every single-descriptor test and fails exactly here);
  * `sort-selector`   — the selector form, called as a SCALAR rather than through
                        -performSelector: (which is the crash F9's probe found);
  * `sort-comparator` — the comparator-block form;
  * `sort-function`   — the C-function form, with its `context` handed through (the counter);
  * `sort-mutable`    — the two mutable sorts;
  * `sort-nil-value`  — a nil value raises, and a ONE-element array (nothing to compare) is what
                        says the rule is about a comparison rather than about the descriptor;
  * `sort-refusals`   — `-allowEvaluation` and the coder forms are ABSENT;
  * `cross-tu`        — a descriptor built in the other unit sorts here.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_sort"
CHECKS = ("sort-descriptor", "sort-reversed", "sort-array", "sort-chain",
          "sort-stable", "sort-selector", "sort-comparator", "sort-function",
          "sort-mutable", "sort-nil-value", "sort-refusals", "cross-tu")


class Case(BaseCase):
    title = "NSSortDescriptor: a sort as a value, resolved through KVC"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_sort")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-SORT-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-SORT "):
                self.note(line)

        done = "FOUNDATION-SORT DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-SORT DONE; output tail: "
                        + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-SORT %s ok$" % re.escape(c),
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

        tally = re.search(r"FOUNDATION-SORT RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-SORT-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
