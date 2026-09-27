# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSPresentationIntent — §62.65's acceptance, and the last row of Strings with Metadata.

docs/design/foundation-plan.md §62.65. The class is the OBJECT the W10 vocabulary has been pointing at since the
attributes shipped: `NSPresentationIntentAttributeName` names its value, `NSPresentationIntentKind` is its kind,
and `NSPresentationIntentTableColumnAlignment` aligns its table's columns — all three declared in
NSAttributedString.h, which the new header imports for them.

Twelve factories, one per kind, and no `-init`: an intent's fields are decided by WHAT KIND OF THING IT IS, so
Apple gives one factory per kind and no way to build a half-formed one.

The probe is `/System/Shared/tests/foundation_presentationintent`, ONE unit, importing only
`<Foundation/Foundation.h>`.

  * `each-factory-answers-its-kind-and-identity` — twelve factories, twelve kinds, twelve identities;
  * `each-kind-carries-the-field-its-factory-set` — the header's level, the item's ordinal, the row's number, the
                                     cell's column, the table's column count and alignments, the code block's hint;
  * `the-parent-link-is-the-chain` — and the top of a document has none;
  * `an-indentation-level-counts-nested-lists` — APPLE'S OWN EXAMPLE, clause by clause: the initial list is 0, its
                                     item is 0 (the clause that decides the implementation — "all elements within
                                     the same list have the same indentation level"), a list nested inside that
                                     item is 1, and its item is 1, so the answer must be 0, 0, 1, 1;
  * `a-field-the-kind-does-not-carry-answers-zero-and-alignments-are-nil` — the absence rule Apple publishes for
                                     one field, and OURS for the numbers;
  * `equivalence-ignores-identity-and-notices-a-field` — THE PAIR THAT MATTERS: two intents differing only in
                                     identity are equivalent, two differing in a field are not, so neither
                                     "always YES" nor "always NO" passes;
  * `equivalence-does-not-look-at-the-parent` — our reading of "their attributes": two intents differing only in
                                     where they hang are the case the method exists for;
  * `the-language-hint-is-a-snapshot` — a mutable source is mutated after being handed over;
  * `only-a-table-carries-column-alignments` — Apple's own absence rule, checked from both sides.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_presentationintent"
CHECKS = ("each-factory-answers-its-kind-and-identity",
          "each-kind-carries-the-field-its-factory-set",
          "the-parent-link-is-the-chain",
          "an-indentation-level-counts-nested-lists",
          "a-field-the-kind-does-not-carry-answers-zero-and-alignments-are-nil",
          "equivalence-ignores-identity-and-notices-a-field",
          "equivalence-does-not-look-at-the-parent",
          "the-language-hint-is-a-snapshot",
          "only-a-table-carries-column-alignments")


class Case(BaseCase):
    title = "NSPresentationIntent: twelve kinds, the parent chain, and equivalence without identity"
    tier = "fast"
    # No fixture and no file system work: it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_presentationintent")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-PRESENTATIONINTENT-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-PRESENTATIONINTENT "):
                self.note(line)

        done = "FOUNDATION-PRESENTATIONINTENT DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-PRESENTATIONINTENT DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-PRESENTATIONINTENT %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-PRESENTATIONINTENT RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-PRESENTATIONINTENT-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
